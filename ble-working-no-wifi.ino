#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

WebServer server(80);
Preferences preferences;

String ssid = "";
String password = "";

// ─── UUIDs — must match bleService.js ────────────────────────────
#define SERVICE_UUID        "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// ─── Advertising ─────────────────────────────────────────────────

void startAdvertising() {
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising as 'Synapse'...");
}

// ─── BLE Callbacks ───────────────────────────────────────────────

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("==============================");
    Serial.println("  BLE CONNECTED!");
    Serial.print  ("  Device count: ");
    Serial.println(pServer->getConnectedCount());
    Serial.println("==============================");
    pCharacteristic->setValue("status:ready");
    pCharacteristic->notify();
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("------------------------------");
    Serial.println("  BLE DISCONNECTED.");
    Serial.println("  Re-advertising...");
    Serial.println("------------------------------");
    delay(300);
    startAdvertising();
  }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    value.trim();

    if (value.length() == 0) return;

    Serial.print("BLE rx: ");
    Serial.println(value);

    if (value.startsWith("user_id:")) {
      String userId = value.substring(8);
      preferences.begin("synapse", false);
      preferences.putString("user_id", userId);
      preferences.end();
      Serial.print("Saved user_id: ");
      Serial.println(userId);
      pCharacteristic->setValue("ok:user_id");
      pCharacteristic->notify();

    } else if (value == "start_rec") {
      pCharacteristic->setValue("ok:recording");
      pCharacteristic->notify();

    } else if (value == "stop_rec") {
      pCharacteristic->setValue("ok:stopped");
      pCharacteristic->notify();

    } else if (value == "get_status") {
      pCharacteristic->setValue("status:ready");
      pCharacteristic->notify();

    } else {
      pCharacteristic->setValue("err:unknown_cmd");
      pCharacteristic->notify();
    }
  }
};

// ─── BLE Init ────────────────────────────────────────────────────

void startBLE() {
  BLEDevice::init("Synapse");

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P4);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     ESP_PWR_LVL_P4);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    ESP_PWR_LVL_P4);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ  |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pCharacteristic->setValue("Synapse ready");

  pService->start();
  startAdvertising();
  Serial.println("BLE started. Device name: Synapse");
}

// ─── HTML Page ───────────────────────────────────────────────────

String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>Synapse WiFi Setup</title></head>
<body style="font-family:sans-serif;text-align:center;margin-top:50px;">
  <h2>Connect Synapse to WiFi</h2>
  <form action="/save">
    <input name="ssid" placeholder="WiFi Name" required><br><br>
    <input name="password" placeholder="Password" type="password" required><br><br>
    <button type="submit">Connect</button>
  </form>
</body>
</html>
)rawliteral";

// ─── WiFi AP Mode ────────────────────────────────────────────────

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP("Synapse_Setup", "synapse123", 6);
  delay(500);

  Serial.println("==============================");
  Serial.println("  WiFi AP STARTED");
  Serial.println("  SSID:     Synapse_Setup");
  Serial.println("  Password: synapse123");
  Serial.print  ("  Portal:   http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("==============================");

  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });

  server.on("/save", []() {
    ssid     = server.arg("ssid");
    password = server.arg("password");

    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();

    Serial.print("WiFi credentials saved. SSID: ");
    Serial.println(ssid);

    server.send(200, "text/html", "<h3>Saved! Rebooting...</h3>");
    delay(2000);
    ESP.restart();
  });

  server.begin();
  Serial.println("Web server started on port 80.");
}

// ─── WiFi STA Mode ───────────────────────────────────────────────

bool connectToWiFi() {
  preferences.begin("wifi", true);
  ssid     = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  preferences.end();

  if (ssid == "") {
    Serial.println("No saved WiFi credentials.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n==============================");
    Serial.println("  WiFi CONNECTED!");
    Serial.print  ("  IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("==============================");
    return true;
  }

  Serial.println("\nWiFi connection failed.");
  return false;
}

// ─── Setup ───────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);

  Serial.println("SYNAPSE BOOTING...");

  if (!connectToWiFi()) {
    Serial.println("Starting WiFi setup mode...");
    
    // ❌ STOP BLE COMPLETELY
    BLEDevice::deinit(true);

    startAccessPoint();
  } else {
    // ✅ ONLY start BLE after WiFi works
    startBLE();
    Serial.println("Device fully online!");
  }
}

// ─── Loop ────────────────────────────────────────────────────────

void loop() {
  server.handleClient();
  delay(10);
}