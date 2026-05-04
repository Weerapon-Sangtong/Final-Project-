// ============================================================================
// 📹 SMART PET FEEDER - CAMERA FIRMWARE (ESP32-CAM)
// ============================================================================

// ============================================================================
// 🔴 หมวดที่ 0: MASTER DEBUG SWITCH
// ============================================================================
// เปลี่ยนเป็น 1 = เปิดข้อความ Debug ผ่าน Serial Monitor
// เปลี่ยนเป็น 0 = ปิดข้อความ Debug เพื่อลดภาระ RAM/Serial
// หมายเหตุ: Serial เส้นนี้ใช้รับข้อมูล WiFi/Token จาก ESP32 Main ด้วย
#define DEBUG_MODE 0

#if DEBUG_MODE == 1
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// ============================================================================
// 📚 หมวดที่ 1: LIBRARIES
// ============================================================================
#include "esp_camera.h"         // ไลบรารีควบคุมกล้อง ESP32-CAM
#include <WiFi.h>               // ไลบรารีเชื่อมต่อ WiFi
#include <ArduinoWebsockets.h>  // ไลบรารี WebSocket สำหรับส่งภาพและรับคำสั่งจาก Server
#include "soc/soc.h"            // ไลบรารีระดับฮาร์ดแวร์ของ ESP32
#include "soc/rtc_cntl_reg.h"   // ใช้ปิด Brown-out detector ตอนกล้องดึงกระแสสูง
#include <ArduinoOTA.h>         // ระบบอัปโหลดโค้ดผ่าน WiFi (OTA)

using namespace websockets;

// ============================================================================
// ⚙️ หมวดที่ 2: SERVER / DEVICE CONFIG
// ============================================================================
const char* server_ip = "34.45.167.7";  // IP ของ WebSocket server
const uint16_t server_port = 4000;      // Port ของ WebSocket server

// ค่าเริ่มต้นจะถูกแทนที่ด้วยค่าที่ ESP32 Main ส่งมาทาง Serial
String deviceId = "PET-001";
String camToken = "PET-001-8K72";

// ============================================================================
// 🔌 หมวดที่ 3: CAMERA PIN DEFINITIONS (AI-Thinker ESP32-CAM)
// ============================================================================
// ขาเหล่านี้เป็นขาประจำของบอร์ด AI-Thinker ESP32-CAM ไม่ควรเปลี่ยน
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
#define FLASH_GPIO_NUM 4  // ขาไฟแฟลช LED บนบอร์ด ESP32-CAM

// ============================================================================
// ⏱️ หมวดที่ 4: CONSTANTS / TIMERS
// ============================================================================
const int captureInterval = 150;                   // ส่งภาพทุก 150ms ประมาณ 6-7 FPS
const unsigned long FLASH_DURATION = 1000;         // เปิดแฟลช 1000ms = 1 วินาที
const unsigned long WS_RECONNECT_INTERVAL = 5000;  // พยายามต่อ WebSocket ใหม่ทุก 5 วินาที
const unsigned long WIFI_TIMEOUT_MS = 30000;       // รอ WiFi สูงสุด 30 วินาที

// ============================================================================
// 🧠 หมวดที่ 5: GLOBAL STATE VARIABLES
// ============================================================================
WebsocketsClient client;

String currentSSID = "";
String currentPASS = "";

unsigned long lastCaptureTime = 0;

bool isFlashOn = false;
unsigned long flashStartTime = 0;

bool isConnectingWiFi = false;
unsigned long wifiStartTime = 0;

bool otaInitialized = false;
bool isOTAUpdating = false;

// ============================================================================
// 📷 หมวดที่ 6: CAMERA INITIALIZATION
// ============================================================================
void init_camera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;  // บีบอัดภาพเป็น JPEG ก่อนส่งผ่าน WebSocket

  // QVGA 320x240 เหมาะกับการสตรีมบน ESP32-CAM เพราะใช้แรมและ bandwidth ไม่สูงเกินไป
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 50;  // เลขน้อยภาพชัดขึ้นแต่ขนาดใหญ่ขึ้น; 50 เน้นความเสถียร/ลื่น
  config.fb_count = 2;       // ใช้ frame buffer 2 ชุด ควรเปิด PSRAM ใน Arduino IDE

  if (esp_camera_init(&config) != ESP_OK) {
    DEBUG_PRINTLN("❌ Camera Init Failed");
    return;
  }

  DEBUG_PRINTLN("✅ Camera Init OK");
}

// ============================================================================
// 🌐 หมวดที่ 7: WIFI CONNECTION
// ============================================================================
void connectToWiFi(String ssid, String pass) {
  if (ssid == "") return;

  // ถ้า SSID เดิมกำลังต่ออยู่แล้ว หรือกำลังเชื่อมต่ออยู่ ไม่ต้องสั่งต่อซ้ำ
  if (ssid == currentSSID && (WiFi.status() == WL_CONNECTED || isConnectingWiFi)) return;

  client.close();
  delay(100);  // รอสั้น ๆ ให้ WebSocket ปิดก่อนเปลี่ยน WiFi

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  // ล้าง static IP เดิม ให้กลับไปใช้ DHCP จาก router/hotspot ใหม่
  WiFi.config(IPAddress(0, 0, 0, 0),
              IPAddress(0, 0, 0, 0),
              IPAddress(0, 0, 0, 0));

  WiFi.disconnect();
  delay(100);  // เว้นจังหวะสั้น ๆ ให้ WiFi stack เคลียร์ connection เดิม

  WiFi.begin(ssid.c_str(), pass.c_str());

  currentSSID = ssid;
  currentPASS = pass;
  isConnectingWiFi = true;
  wifiStartTime = millis();

  DEBUG_PRINTF(">>> 📡 Connecting to WiFi: %s\n", ssid.c_str());
}

void checkWiFiStatus(unsigned long now) {
  // เมื่อ WiFi ต่อสำเร็จครั้งแรก ให้เริ่มระบบ OTA
  if (WiFi.status() == WL_CONNECTED && !otaInitialized) {
    ArduinoOTA.setHostname("Smart-Pet-ESP32-CAM");
    ArduinoOTA.setPassword("1234");

    ArduinoOTA.onStart([]() {
      DEBUG_PRINTLN("\n>>> Start OTA Update for CAM...");

      isOTAUpdating = true;

      client.close();
      delay(300);

      digitalWrite(FLASH_GPIO_NUM, LOW);

      // ปิดกล้องก่อน OTA เพื่อลดการใช้ RAM/PSRAM และลดโอกาส OTA ล้มเหลว
      esp_camera_deinit();
    });

    ArduinoOTA.onError([](ota_error_t error) {
      DEBUG_PRINT("OTA Error: ");
      DEBUG_PRINTLN(error);

      ESP.restart();
    });

    ArduinoOTA.begin();
    otaInitialized = true;
    DEBUG_PRINTLN("☁️ OTA Initialized for CAM");
  }

  if (isConnectingWiFi) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnectingWiFi = false;
      DEBUG_PRINTLN("✅ CAM_CONNECTED_TO_WIFI");
    } else if (now - wifiStartTime > WIFI_TIMEOUT_MS) {
      // ต่อ WiFi ไม่สำเร็จภายใน 30 วินาที ให้หยุดสถานะ connecting ไว้ก่อน รอ Main ส่งข้อมูลมาใหม่
      isConnectingWiFi = false;
      DEBUG_PRINTLN("❌ WiFi Connection Timeout!");
    }
  }
}

// ============================================================================
// 🔗 หมวดที่ 8: SERIAL COMMANDS FROM ESP32 MAIN
// ============================================================================
void checkSerialWiFi() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');

    // เคลียร์ข้อมูลค้างใน buffer เพื่อกัน packet เก่าปนกับ packet ใหม่
    while (Serial.available()) {
      Serial.read();
    }

    data.trim();

    if (data == "REBOOT_CAM") {
      DEBUG_PRINTLN("⚠️ Master requested reboot. Restarting CAM...");
      delay(100);
      ESP.restart();
    }

    // รูปแบบใหม่จาก Main: SSID|PASSWORD|deviceId|token
    int p1 = data.indexOf('|');
    int p2 = data.indexOf('|', p1 + 1);
    int p3 = data.indexOf('|', p2 + 1);

    if (p1 > 0 && p2 > p1 && p3 > p2) {
      String newSSID = data.substring(0, p1);
      String newPASS = data.substring(p1 + 1, p2);

      deviceId = data.substring(p2 + 1, p3);
      camToken = data.substring(p3 + 1);

      DEBUG_PRINTLN("Received WiFi + Pairing Config from Main");

      connectToWiFi(newSSID, newPASS);

    } else {
      // Fallback: รองรับ Main เวอร์ชันเก่าที่ส่ง SSID,PASSWORD
      int commaIndex = data.indexOf(',');

      if (commaIndex > 0) {
        String newSSID = data.substring(0, commaIndex);
        String newPASS = data.substring(commaIndex + 1);

        DEBUG_PRINTLN("Received WiFi only from Main");
        connectToWiFi(newSSID, newPASS);
      }
    }
  }
}

// ============================================================================
// 🌐 หมวดที่ 9: WEBSOCKET / CAMERA STREAM
// ============================================================================
void onMessageCallback(WebsocketsMessage message) {
  String data = message.data();

  // ถ้า server ส่งคำสั่ง feed_now ให้เปิดแฟลช 1 วินาทีเพื่อช่วยส่องตอนให้อาหาร
  if (data.indexOf("feed_now") >= 0) {
    if (!isFlashOn) {
      digitalWrite(FLASH_GPIO_NUM, HIGH);
      isFlashOn = true;
      flashStartTime = millis();
      DEBUG_PRINTLN("💡 Flash ON!");
    }
  }
}

void processCameraStream(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    client.close();
    return;
  }

  static unsigned long lastWsConnect = 0;

  client.poll();

  if (!client.available()) {
    if (now - lastWsConnect > WS_RECONNECT_INTERVAL) {
      lastWsConnect = now;

      DEBUG_PRINTLN(">>> 🌐 Connecting to Camera WebSocket...");

      client.close();
      delay(50);  // รอสั้น ๆ ก่อน reconnect WebSocket เพื่อลดการสะดุดของกล้อง

      if (client.connect(server_ip, server_port, "/")) {
        client.onMessage(onMessageCallback);

        client.send("{\"type\":\"register\", \"deviceId\":\"" + deviceId + "\", \"role\":\"camera\", \"token\":\"" + camToken + "\"}");

        DEBUG_PRINTLN("✅ Camera WebSocket Connected!");
      } else {
        DEBUG_PRINTLN("❌ Camera WebSocket Failed");
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

// ============================================================================
// 💡 หมวดที่ 10: FLASH LED
// ============================================================================
void checkFlashTimeout(unsigned long now) {
  if (isFlashOn) {
    if (now - flashStartTime >= FLASH_DURATION) {
      digitalWrite(FLASH_GPIO_NUM, LOW);
      isFlashOn = false;
      DEBUG_PRINTLN("💡 Flash OFF");
    }
  }
}

// ============================================================================
// ☁️ หมวดที่ 11: OTA / DEBUG TOOLS
// ============================================================================
void checkESP32_RAM(unsigned long now) {
  static unsigned long lastRamCheck = 0;

  if (now - lastRamCheck > 10000) {
    lastRamCheck = now;

    uint32_t freeRam = ESP.getFreeHeap();
    uint32_t totalRam = ESP.getHeapSize();
    uint8_t ramPercent = (freeRam * 100) / totalRam;

    uint32_t freePsram = ESP.getFreePsram();
    uint32_t totalPsram = ESP.getPsramSize();
    uint8_t psramPercent = 0;

    if (totalPsram > 0) {
      psramPercent = (freePsram * 100) / totalPsram;
    }

    DEBUG_PRINTF("🧠 [CAM MEMORY] SRAM: %d bytes (%d%% free) | PSRAM: %d bytes (%d%% free)\n",
                 freeRam, ramPercent, freePsram, psramPercent);
  }
}

// ============================================================================
// 🎬 หมวดที่ 12: SETUP / LOOP
// ============================================================================
void setup() {
  // ปิด Brown-out detector เพื่อลดอาการรีเซ็ตตอนกล้องดึงกระแสสูง
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Serial นี้ใช้ทั้งรับข้อมูลจาก ESP32 Main และแสดง Debug เมื่อ DEBUG_MODE = 1
  Serial.begin(115200);
  Serial.setTimeout(30);  // ลดเวลารอ readStringUntil() เพื่อไม่ให้ loop ค้างนาน

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  init_camera();

  client.onMessage(onMessageCallback);
}

void loop() {
  if (otaInitialized) {
    ArduinoOTA.handle();
  }

  if (isOTAUpdating) {
    return;
  }

  unsigned long now = millis();

  checkFlashTimeout(now);
  checkSerialWiFi();
  checkWiFiStatus(now);
  processCameraStream(now);

  // เปิดใช้เมื่อต้องการ debug RAM/PSRAM
  // checkESP32_RAM(now);
}