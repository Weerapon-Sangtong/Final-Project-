// ============================================================================
// 📹 SMART PET FEEDER - CAMERA FIRMWARE (ESP32-CAM)
// ============================================================================
#define DEBUG_MODE 1

#if DEBUG_MODE == 1
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)     
  #define DEBUG_PRINTLN(x)   
  #define DEBUG_PRINTF(...)  
#endif

#include "esp_camera.h"         
#include <WiFi.h>               
#include <ArduinoWebsockets.h>  
#include "soc/soc.h"            
#include "soc/rtc_cntl_reg.h"   
#include <ArduinoOTA.h>         

using namespace websockets;
WebsocketsClient client;

const char* server_ip = "34.45.167.7";   
const uint16_t server_port = 4000;       
const char* myToken = "ESP32-CAM-001";   

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define FLASH_GPIO_NUM 4  

const int captureInterval = 100;                   // 🌟 เปลี่ยนเป็น 100ms เพื่อความลื่นไหลและไม่หน่วงระบบ
const unsigned long FLASH_DURATION = 1000;         
const unsigned long WS_RECONNECT_INTERVAL = 5000;  
const unsigned long WIFI_TIMEOUT_MS = 30000;       // 🌟 ให้เวลาต่อเน็ต 30 วินาที

String currentSSID = "";
String currentPASS = "";
unsigned long lastCaptureTime = 0;
bool isFlashOn = false;
unsigned long flashStartTime = 0;
bool isConnectingWiFi = false;
unsigned long wifiStartTime = 0;
bool otaInitialized = false;  
bool isOTAUpdating = false;

void init_camera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG; 
  config.frame_size = FRAMESIZE_QVGA;  
  config.jpeg_quality = 50;            
  config.fb_count = 2;                 

  if (esp_camera_init(&config) != ESP_OK) {
    DEBUG_PRINTLN("❌ Camera Init Failed");
    return;  
  }
  DEBUG_PRINTLN("✅ Camera Init OK");
}

void connectToWiFi(String ssid, String pass) {
  if (ssid == "") return;

  // 🌟 บังคับปิดท่อ WebSocket ก่อนเสมอ ป้องกันท่อค้างเวลาต่อเน็ตใหม่
  client.close();
  delay(500);

  WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();                       
  WiFi.begin(ssid.c_str(), pass.c_str());  

  currentSSID = ssid;
  currentPASS = pass;
  isConnectingWiFi = true;
  wifiStartTime = millis();
}

void onMessageCallback(WebsocketsMessage message) {
  String data = message.data();
  if (data.indexOf("feed_now") >= 0) { 
    if (!isFlashOn) {                      
      digitalWrite(FLASH_GPIO_NUM, HIGH);  
      isFlashOn = true;                    
      flashStartTime = millis();           
    }
  }
}

void checkFlashTimeout(unsigned long now) {
  if (isFlashOn) {
    if (now - flashStartTime >= FLASH_DURATION) {
      digitalWrite(FLASH_GPIO_NUM, LOW);  
      isFlashOn = false;                  
    }
  }
}

void checkSerialWiFi() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();

    if (data == "REBOOT_CAM") {
      delay(100);
      ESP.restart(); 
    }

    int commaIndex = data.indexOf(',');
    if (commaIndex > 0) {
      String newSSID = data.substring(0, commaIndex);
      String newPASS = data.substring(commaIndex + 1);
      connectToWiFi(newSSID, newPASS);  
    }
  }
}

void processCameraStream(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    client.close(); // 🌟 ถ้าเน็ตหลุด ให้ปิด Socket 
    return;
  }

  static unsigned long lastWsConnect = 0;
  
  client.poll();

  if (!client.available()) {
    if (now - lastWsConnect > WS_RECONNECT_INTERVAL) {  
      lastWsConnect = now;
      client.close(); // 🌟 บังคับปิดก่อนเปิดใหม่
      delay(300);
      if (client.connect(server_ip, server_port, "/")) {
        client.send("{\"type\":\"register\", \"token\":\"" + String(myToken) + "\"}");
        client.onMessage(onMessageCallback);
      }
    }
    return;
  }

  if (now - lastCaptureTime > captureInterval) {
    lastCaptureTime = now;
    camera_fb_t* fb = esp_camera_fb_get(); 
    if (fb) {
      client.sendBinary((const char*)fb->buf, fb->len); 
      esp_camera_fb_return(fb);  
    }
  }
}

void checkWiFiStatus(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED && !otaInitialized) {
      ArduinoOTA.setHostname("Smart-Pet-ESP32-CAM");
      ArduinoOTA.setPassword("1234");
      ArduinoOTA.onStart([]() {
        isOTAUpdating = true; 
        client.close(); 
      });
      ArduinoOTA.begin();
      otaInitialized = true;
  }

  if (isConnectingWiFi) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnectingWiFi = false;
    } else if (now - wifiStartTime > WIFI_TIMEOUT_MS) {
      isConnectingWiFi = false; 
    }
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // 🌟 ปรับตามบทวิเคราะห์ให้เร็วขึ้น (ต้องตรงกับบอร์ดแม่)
  Serial.begin(115200);    
  Serial.setTimeout(10); 

  // 🌟 บังคับตั้งค่า Reconnect ตั้งแต่เริ่ม
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  init_camera();
}

void loop() {
  if (otaInitialized) ArduinoOTA.handle();
  if (isOTAUpdating) return; 

  unsigned long now = millis();  
  checkFlashTimeout(now);        
  checkSerialWiFi();             
  checkWiFiStatus(now);          
  processCameraStream(now);      
}