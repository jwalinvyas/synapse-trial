#include <driver/i2s.h>

#define BUTTON_PIN D0
#define LED D1

#define I2S_WS  D9
#define I2S_SD  D10
#define I2S_SCK D8
#define I2S_PORT I2S_NUM_0

bool isRecording = false;
bool lastButtonState = HIGH;
unsigned long lastDebounce = 0;

#define DEBOUNCE_DELAY 50

void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);

  Serial.println("Mic ready");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED, OUTPUT);

  digitalWrite(LED, LOW);

  setupI2S();

  Serial.println("Ready — Press button to record");
}

void loop() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > DEBOUNCE_DELAY) {
    if (reading == LOW) {
      if (!isRecording) {
        isRecording = true;
        digitalWrite(LED, HIGH);
        Serial.println("Recording — speak now!");
      } else {
        isRecording = false;
        digitalWrite(LED, LOW);
        Serial.println("Stopped");
      }
      delay(300);
    }
  }

  lastButtonState = reading;

  if (isRecording) {
    int32_t buffer[256];
    size_t bytesRead;

    i2s_read(I2S_PORT, &buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);

    // No bit shifting — use raw value
    int32_t maxVal = 0;
    int samples = bytesRead / 4;

    for (int i = 0; i < samples; i++) {
      int32_t val = abs(buffer[i]);
      if (val > maxVal) maxVal = val;
    }

    Serial.print("Volume: ");
    Serial.println(maxVal);
  }
}