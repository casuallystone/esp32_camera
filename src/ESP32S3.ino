#include "ESP_I2S.h"
#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_timer.h"

#define CAMERA_MODEL_XIAO_ESP32S3

#include "camera_pins.h"

const int SD_PIN_CS = 21;
const int I2S_SCK_PIN = 42;
const int I2S_SD_PIN = 41;

I2SClass i2s;
File dataFile;

bool camera_initialized = false;
bool sd_initialized = false;
bool i2s_initialized = false;

int audioFileCount = 0;
int videoFileCount = 0;

void initializeSDCard();
void initializeI2S();
void initializeCamera();
void recordAudio(int durationSeconds);
void recordVideo(int durationSeconds);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.println("\n--- ESP32 Audio/Video Recorder ---");

  initializeSDCard();
  initializeI2S();
  initializeCamera();

  Serial.println("\nReady for commands:");
  Serial.println("  1: Record 5 seconds of audio to SD card.");
  Serial.println("  2: Record 10 seconds of video to SD card.");
}

void loop() {
  if (Serial.available()) {
    char command = Serial.read();

    if (command == '1') {
      Serial.println("\nCommand '1' received: Starting audio recording...");
      recordAudio(5);
    } else if (command == '2') {
      Serial.println("\nCommand '2' received: Starting video recording...");
      recordVideo(10);
    } else {
      Serial.print("Unknown command: '");
      Serial.print(command);
      Serial.println("'. Please enter '1' or '2'.");
    }
  }

  delay(100);
}

void initializeSDCard() {
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_PIN_CS)) {
    Serial.println("Failed to mount SD Card! Please check connections.");
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached.");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  sd_initialized = true;
  Serial.println("SD card initialized successfully.");
}

void initializeI2S() {
  Serial.println("Initializing I2S bus (microphone)...");
  i2s.setPinsPdmRx(I2S_SCK_PIN, I2S_SD_PIN);

  if (!i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S! Check microphone connections.");
    return;
  }
  i2s_initialized = true;
  Serial.println("I2S bus initialized successfully.");
}

void initializeCamera() {
  Serial.println("Initializing camera...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y4_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }
  camera_initialized = true;
  Serial.println("Camera initialized successfully.");
}

void recordAudio(int durationSeconds) {
  if (!i2s_initialized) {
    Serial.println("Error: I2S (microphone) not initialized.");
    return;
  }
  if (!sd_initialized) {
    Serial.println("Error: SD card not initialized.");
    return;
  }

  Serial.printf("Recording %d seconds of audio data...\n", durationSeconds);

  uint8_t *wav_buffer;
  size_t wav_size;

  wav_buffer = i2s.recordWAV(durationSeconds, &wav_size);

  char filename[32];
  sprintf(filename, "/audio_rec_%d.wav", audioFileCount++);

  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile) {
    Serial.printf("Failed to open file %s for writing!\n", filename);
    if (wav_buffer) {
      free(wav_buffer);
    }
    return;
  }

  Serial.printf("Writing audio data to %s...\n", filename);

  if (dataFile.write(wav_buffer, wav_size) != wav_size) {
    Serial.println("Failed to write audio data to file!");
  } else {
    Serial.printf("Audio data saved to %s (size: %u bytes).\n", filename, wav_size);
  }

  dataFile.close();
  if (wav_buffer) {
    free(wav_buffer);
  }
  Serial.println("Audio recording complete.");
}

void recordVideo(int durationSeconds) {
  if (!camera_initialized) {
    Serial.println("Error: Camera not initialized.");
    return;
  }
  if (!sd_initialized) {
    Serial.println("Error: SD card not initialized.");
    return;
  }

  Serial.printf("Recording %d seconds of video data...\n", durationSeconds);
  Serial.println("Note: The recorded video may not be playable without a proper decoder/container format.");

  char filename[32];
  sprintf(filename, "/video_rec_%d.avi", videoFileCount++);

  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile) {
    Serial.printf("Error opening video file %s!\n", filename);
    return;
  }

  Serial.printf("Capturing frames to %s...\n", filename);

  unsigned long startTime = millis();
  unsigned long captureDurationMs = durationSeconds * 1000;
  int framesCaptured = 0;

  while ((millis() - startTime) < captureDurationMs) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Error getting framebuffer!");
      break;
    }

    dataFile.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    framesCaptured++;
  }

  dataFile.close();
  Serial.printf("Video saved to %s. Captured %d frames.\n", filename, framesCaptured);
  Serial.println("Video recording complete.");
}