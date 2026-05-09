#include "esp_camera.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#define CAMERA_MODEL_ESP32S3_EYE 
#include "camera_pins.h"

// --- CẤU HÌNH UART2 (Chân TX/RX vật lý trên ESP32-S3) ---
#define RX_PIN 44 
#define TX_PIN 43 

const char* ssid = "ACTVN-TB5";
const char* password = "12345679";

void startCameraServer();
static QueueHandle_t serialQueue = NULL;
static TaskHandle_t serialTaskHandle = NULL;
SemaphoreHandle_t serialMutex = NULL;

// Mảng trạng thái 16 ghế (0: Tài xế, 1-15: Hành khách)
bool seat_state[16] = {false}; 
extern bool face_detected_global; 

// Buffer cho Serial2 (thay vì String)
#define SERIAL_BUFFER_SIZE 64
struct SerialMessage {
  char data[SERIAL_BUFFER_SIZE];
  uint16_t len;
};

// Task xử lý Serial2 không block stream - Tối ưu
void serialTask(void *pvParameters) {
  SerialMessage msg;
  while(1) {
    if (xQueueReceive(serialQueue, &msg, 100)) {
      Serial2.write((const uint8_t*)msg.data, msg.len);
      Serial2.write('\n');
    }
  }
}

void queueSerialMessage(const char* msg, uint16_t len) {
  if (serialQueue != NULL && len < SERIAL_BUFFER_SIZE) {
    SerialMessage serialMsg;
    memcpy(serialMsg.data, msg, len);
    serialMsg.len = len;
    xQueueSend(serialQueue, &serialMsg, 0);
  }
}

// Wrapper cho compatibility
void queueSerialMessage(String msg) {
  queueSerialMessage(msg.c_str(), msg.length());
}

void setup() {
  // Khởi tạo Serial2 để giao tiếp
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial2.setRxBufferSize(256);
  
  // Tạo queue và task cho Serial2
  serialQueue = xQueueCreate(20, sizeof(SerialMessage));
  serialMutex = xSemaphoreCreateMutex();
  
  xTaskCreatePinnedToCore(serialTask, "SerialTask", 2048, NULL, 2, &serialTaskHandle, 0);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  // Cấu hình tối ưu cho nhận diện và stream
  config.xclk_freq_hz = 20000000; 
  config.frame_size = FRAMESIZE_QVGA; 
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  // Khởi tạo Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    delay(1000);
    ESP.restart();
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -1);
  }
  
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  startCameraServer();
}

void loop() {
  if (Serial2.available() > 0) {
    int byte1 = Serial2.read();
    if ((byte1 == 'm' || byte1 == 'x') && Serial2.available() > 0) {
      char actionType = (char)byte1;
      char seatStr[3] = {0};
      uint8_t idx = 0;
      
      while (Serial2.available() && idx < 2) {
        char c = Serial2.read();
        if (c >= '0' && c <= '9') {
          seatStr[idx++] = c;
        } else if (c == '\r' || c == '\n') {
          break;
        }
      }
      
      if (idx > 0) {
        seatStr[idx] = '\0';
        int seatNum = atoi(seatStr);
        
        if (seatNum >= 0 && seatNum <= 15) {
          if (xSemaphoreTake(serialMutex, 10)) {
            if (actionType == 'm') { seat_state[seatNum] = true; } 
            else { seat_state[seatNum] = false; }
            xSemaphoreGive(serialMutex);
          }
        }
      }
    }
  }
  
  vTaskDelay(pdMS_TO_TICKS(5));
}