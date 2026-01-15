#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "driver/i2s.h"

/* ================= PIN DEFINITIONS ================= */
#define BUTTON_PIN 0
#define LED_PIN    2

/* I2S MIC (INMP441)*/
#define MIC_BCLK   26
#define MIC_LRCLK  25
#define MIC_DOUT   33

/* I2S SPEAKER (MAX98357A)*/
#define SPK_BCLK   14
#define SPK_LRCLK  27
#define SPK_DOUT   22

/* ================= AUDIO CONFIG ================= */
#define SAMPLE_RATE     16000
#define RECORD_SECONDS  5
#define CHUNK_SIZE      320 

/* ================= BLE UUIDs ================= */
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHAR_AUDIO_TX       "abcd1111-1234-1234-1234-abcdefabcdef"
#define CHAR_AUDIO_RX       "abcd2222-1234-1234-1234-abcdefabcdef"

BLECharacteristic *txChar;
bool deviceConnected = false;

/* ================= BLE CALLBACKS ================= */
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) {
    deviceConnected = true;
    digitalWrite(LED_PIN, HIGH);  
  }

  void onDisconnect(BLEServer*) {
    deviceConnected = false;
    digitalWrite(LED_PIN, LOW);
  }
};

class RXCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    std::string data = c->getValue();
    if (data.length() > 0) {
      size_t bytesWritten;
      i2s_write(I2S_NUM_1, data.data(), data.length(), &bytesWritten, portMAX_DELAY);
    }
  }
};

/* ================= I2S SETUP ================= */
void setupMic() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = MIC_BCLK,
    .ws_io_num = MIC_LRCLK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_DOUT
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

void setupSpeaker() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRCLK,
    .data_out_num = SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
}

/* ================= RECORD FUNCTION ================= */
void recordAndSend() {
  if (!deviceConnected) return;

  uint8_t buffer[CHUNK_SIZE];
  size_t bytesRead;

  unsigned long start = millis();
  while (millis() - start < RECORD_SECONDS * 1000) {
    i2s_read(I2S_NUM_0, buffer, CHUNK_SIZE, &bytesRead, portMAX_DELAY);
    txChar->setValue(buffer, bytesRead);
    txChar->notify();
    delay(10);
  }
}

/* ================= SETUP ================= */
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  setupMic();
  setupSpeaker();

  BLEDevice::init("ESP32-AUDIO");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  txChar = service->createCharacteristic(
    CHAR_AUDIO_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txChar->addDescriptor(new BLE2902());

  BLECharacteristic *rxChar = service->createCharacteristic(
    CHAR_AUDIO_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  rxChar->setCallbacks(new RXCallbacks());

  service->start();
  server->getAdvertising()->start();
}

/* ================= LOOP ================= */
void loop() {
  if (digitalRead(BUTTON_PIN) == LOW && deviceConnected) {
    digitalWrite(LED_PIN, HIGH);
    recordAndSend();
    digitalWrite(LED_PIN, LOW); 
    delay(1000);
  }
}