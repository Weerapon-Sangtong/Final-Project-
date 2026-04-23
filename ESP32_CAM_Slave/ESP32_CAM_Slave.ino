// ============================================================================
// 📹 SMART PET FEEDER - CAMERA FIRMWARE (ESP32-CAM)
// ============================================================================

// ==========================================
// 🔴 หมวดที่ 0: MASTER DEBUG SWITCH (สวิตช์ข้อความหลังบ้าน)
// ==========================================
// 💡 เปลี่ยนเป็น 1 = เปิดดูสถานะผ่าน Serial Monitor, เปลี่ยนเป็น 0 = ปิดข้อความเพื่อประหยัด RAM
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

// ==========================================
// 📚 หมวดที่ 1: LIBRARIES (เรียกใช้ไลบรารี)
// ==========================================
#include "esp_camera.h"         // ไลบรารีคุมเลนส์กล้อง
#include <WiFi.h>               // ไลบรารีคุม WiFi
#include <ArduinoWebsockets.h>  // ไลบรารีคุมการสตรีมมิ่งสด
#include "soc/soc.h"            // จัดการระดับฮาร์ดแวร์
#include "soc/rtc_cntl_reg.h"   // คุมเรื่องพลังงาน (ใช้ปิดเซนเซอร์ไฟตก)
#include <ArduinoOTA.h>         // ระบบอัปเดตโค้ดผ่าน WiFi

using namespace websockets;
WebsocketsClient client;

// ==========================================
// ⚙️ หมวดที่ 2: NETWORK & SERVER SETTINGS
// ==========================================
const char* server_ip = "34.45.167.7";   // IP ของเซิร์ฟเวอร์
const uint16_t server_port = 4000;       // Port สำหรับส่งข้อมูล
const char* myToken = "ESP32-CAM-001";   // รหัสยืนยันตัวตนของกล้อง

// ==========================================
// 🔌 หมวดที่ 3: CAMERA PIN DEFINITIONS (ขาอุปกรณ์ของเลนส์)
// ==========================================
// 💡 ขาพวกนี้เป็นสเปกตายตัวของบอร์ด AI-Thinker ESP32-CAM ห้ามเปลี่ยนตัวเลขเด็ดขาด!
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
#define FLASH_GPIO_NUM 4  // ขาสำหรับหลอดไฟแฟลช LED

// ==========================================
// ⏱️ หมวดที่ 4: TIMERS & VARIABLES (ตัวแปรระบบ)
// ==========================================
const int captureInterval = 50;                    // ความถี่การส่งภาพ (50ms = 20 เฟรมต่อวินาที เน้นเสถียร)
const unsigned long FLASH_DURATION = 1000;         // เวลาเปิดไฟแฟลช (1000ms = 1 วินาที)
const unsigned long WS_RECONNECT_INTERVAL = 5000;  // ดีเลย์รอต่อ WebSocket ใหม่ (5 วินาที)
const unsigned long WIFI_TIMEOUT_MS = 10000;       // เวลาสูงสุดในการพยายามเชื่อม WiFi (10 วินาที)

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
// ==================== 🛠️ หมวดที่ 5: CORE FUNCTIONS ===========================
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
  config.pixel_format = PIXFORMAT_JPEG; // บีบอัดภาพเป็น JPEG

  // 💡 การตั้งค่าความคมชัด: FRAMESIZE_QVGA (320x240) เหมาะกับการสตรีมมิ่งที่สุด
  config.frame_size = FRAMESIZE_QVGA;  
  config.jpeg_quality = 50;            // เลขน้อยภาพชัด, เลขมากภาพแตก (ตั้ง 50 คือเน้นลื่นไหล)
  config.fb_count = 2;                 // สำคัญ! จองแรมไว้เก็บภาพ 2 เฟรม (ต้องเปิด PSRAM ในเมนู Tools ด้วย)

  // 💡 ถ้ากล้องพัง สายแพหลุด หรือลืมเปิด PSRAM มันจะทำงานเข้า if ตัวนี้แล้วจบการทำงานเลย
  if (esp_camera_init(&config) != ESP_OK) {
    DEBUG_PRINTLN("❌ Camera Init Failed");
    return;  
  }
  DEBUG_PRINTLN("✅ Camera Init OK");
}

void connectToWiFi(String ssid, String pass) {
  if (ssid == "") return;

  // ดักจับ: ถ้าส่งรหัสเดิมมา แล้วบอร์ดกำลังต่อเน็ตอยู่ ให้ข้ามไปเลย
  if (ssid == currentSSID && (WiFi.status() == WL_CONNECTED || isConnectingWiFi)) return;

  // 🌟 บังคับให้ลืม IP เดิม เพื่อไปขอ IP ใหม่จากเราเตอร์ตัวใหม่
  WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  
  // 🌟 สูตรลับความลื่น: บังคับโหมดลูกข่าย และไม่ให้ WiFi แอบหลับ (ช่วยให้สตรีมไม่กระตุก)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.disconnect();                       
  WiFi.begin(ssid.c_str(), pass.c_str());  

  currentSSID = ssid;
  currentPASS = pass;
  isConnectingWiFi = true;
  wifiStartTime = millis();
  
  DEBUG_PRINTF(">>> 📡 Connecting to WiFi: %s\n", ssid.c_str());
}

// 💡 ฟังก์ชันรับคำสั่งจากเซิร์ฟเวอร์
void onMessageCallback(WebsocketsMessage message) {
  String data = message.data();

  // ถ้าระบบสั่งให้อาหาร (feed_now) ให้เปิดไฟแฟลช 1 วิ
  if (data.indexOf("feed_now") >= 0) { // 🌟 ใช้ >= 0 เพื่อให้ตรวจเจอคำสั่งได้ชัวร์ๆ
    if (!isFlashOn) {                      
      digitalWrite(FLASH_GPIO_NUM, HIGH);  
      isFlashOn = true;                    
      flashStartTime = millis();           
      DEBUG_PRINTLN("💡 Flash ON!");
    }
  }
}

// 💡 ฟังก์ชันนับเวลาปิดไฟแฟลช
void checkFlashTimeout(unsigned long now) {
  if (isFlashOn) {
    if (now - flashStartTime >= FLASH_DURATION) {
      digitalWrite(FLASH_GPIO_NUM, LOW);  
      isFlashOn = false;                  
      DEBUG_PRINTLN("💡 Flash OFF");
    }
  }
}

// 💡 ฟังก์ชันแอบฟังชื่อ WiFi และคำสั่งจากบอร์ดแม่ผ่านสายไฟ TX/RX
void checkSerialWiFi() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();

    // 🌟 [จุดที่หายไป!] เพิ่มการดักจับคำสั่งรีบูทจากบอร์ดแม่
    if (data == "REBOOT_CAM") {
      DEBUG_PRINTLN("⚠️ Master requested reboot. Restarting CAM...");
      delay(100);
      ESP.restart(); // สั่งให้กล้องรีบูทตัวเองตามบอร์ดแม่ทันที!
    }

    // หั่นข้อความหน้าและหลังลูกน้ำ (,) เพื่อแยกชื่อ WiFi กับ รหัสผ่าน
    int commaIndex = data.indexOf(',');
    if (commaIndex > 0) {
      String newSSID = data.substring(0, commaIndex);
      String newPASS = data.substring(commaIndex + 1);
      connectToWiFi(newSSID, newPASS);  
    }
  }
}

// 💡 ฟังก์ชันหัวใจหลัก: จัดการสตรีมวิดีโอ
void processCameraStream(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) return;  

  static unsigned long lastWsConnect = 0;
  
  // 1. เช็คว่าหลุดจากเซิร์ฟเวอร์ไหม ถ้าหลุดให้พยายามต่อใหม่
  if (!client.available()) {
    if (now - lastWsConnect > WS_RECONNECT_INTERVAL) {  
      lastWsConnect = now;
      DEBUG_PRINTLN(">>> 🌐 Connecting to Camera WebSocket...");
      if (client.connect(server_ip, server_port, "/")) {
        client.send("{\"type\":\"register\", \"token\":\"" + String(myToken) + "\"}");
        DEBUG_PRINTLN("✅ Camera WebSocket Connected!");
      }
    }
  } else {
    client.poll();  // รับข้อมูลขาเข้า
  }

  // 2. จับภาพและยิงขึ้นเซิร์ฟเวอร์ตามรอบเวลา (captureInterval)
  if (now - lastCaptureTime > captureInterval) {
    lastCaptureTime = now;

    if (client.available()) {
      camera_fb_t* fb = esp_camera_fb_get(); // ถ่ายรูป 1 ช็อต
      if (fb) {
        client.sendBinary((const char*)fb->buf, fb->len); // ส่งรูปขึ้นเว็บ
        esp_camera_fb_return(fb);  // คืนหน่วยความจำกลับให้ระบบ
      }
    }
  }
}

void checkWiFiStatus(unsigned long now) {
  // ทันทีที่ต่อเน็ตติดครั้งแรก ให้ปลุกระบบ OTA ขึ้นมารอ
  if (WiFi.status() == WL_CONNECTED && !otaInitialized) {
      ArduinoOTA.setHostname("Smart-Pet-ESP32-CAM");
      ArduinoOTA.setPassword("1234");
      
      ArduinoOTA.onStart([]() {
        DEBUG_PRINTLN("\n>>> Start OTA Update for CAM...");
        isOTAUpdating = true; 
        client.close(); // ปิดการสตรีมมิ่งทันทีเพื่อคืน RAM ให้ระบบ OTA     
      });

      ArduinoOTA.begin();
      otaInitialized = true;
      DEBUG_PRINTLN("☁️ OTA Initialized for CAM");
  }

  if (isConnectingWiFi) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnectingWiFi = false;
      DEBUG_PRINTLN("✅ CAM_CONNECTED_TO_WIFI");
    }
    // หมดเวลาพยายามต่อเน็ต (10 วิ) ให้หยุดพักป้องกันบอร์ดเอ๋อ
    else if (now - wifiStartTime > WIFI_TIMEOUT_MS) {
      isConnectingWiFi = false; 
      DEBUG_PRINTLN("❌ WiFi Connection Timeout!");
    }
  }
}

// 💡 เช็คสถานะหน่วยความจำ (RAM และ PSRAM) ของกล้อง
void checkESP32_RAM(unsigned long now) {
  static unsigned long lastRamCheck = 0;

  // เช็คและแสดงผลทุกๆ 10 วินาที
  if (now - lastRamCheck > 10000) {
    lastRamCheck = now;

    // เช็ค SRAM (แรมหลักของชิป)
    uint32_t freeRam = ESP.getFreeHeap();
    uint32_t totalRam = ESP.getHeapSize();
    uint8_t ramPercent = (freeRam * 100) / totalRam;

    // เช็ค PSRAM (แรมเสริมสำหรับเก็บบัฟเฟอร์ภาพวิดีโอ) *สำคัญมากสำหรับกล้อง
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
// ======================= 🎬 MAIN EXECUTION ====================================
// ============================================================================

void setup() {
  // 🌟 ปิดระบบเซนเซอร์ไฟกระชาก (Brown-out detector) เพื่อกันบอร์ดดับตอนกล้องดึงกระแสไฟ
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(9600);    // 💡 ห้ามใช้ DEBUG_PRINT ตรงนี้ เพราะต้องเอาไว้อ่านค่า WiFi จากบอร์ดแม่จริงๆ
  Serial.setTimeout(10); // ลดเวลา Timeout ป้องกันบอร์ดค้างเวลารอรับ Serial

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  init_camera();

  client.onMessage(onMessageCallback);
}

void loop() {
  // 1. จัดการระบบอัปเดตโค้ดไร้สาย (OTA)
  if (otaInitialized) {
    ArduinoOTA.handle();
  }
  
  // 💡 🗑️ ลบ ArduinoOTA.handle() ซ้ำซ้อนทิ้งไป 1 บรรทัด
  // ถ้ากำลังอัปเดตโค้ดอยู่ ให้ตัดจบการทำงานส่วนอื่นทันที ป้องกัน RAM เต็มจนแครช
  if (isOTAUpdating) {
    return; 
  }

  // 2. ลำดับการทำงานปกติของกล้อง
  unsigned long now = millis();  
  checkFlashTimeout(now);        // เช็คเวลาปิดไฟแฟลช
  checkSerialWiFi();             // แอบฟังชื่อ WiFi จากบอร์ดแม่
  checkWiFiStatus(now);          // จัดการสถานะเน็ตและเปิดระบบ OTA
  processCameraStream(now);      // สตรีมมิ่งวิดีโอ
  
  // 🛠️ โค้ดช่าง: เก็บไว้เช็คเปอร์เซ็นต์ RAM ที่ว่างอยู่
  // checkESP32_RAM(now);        // พิมพ์รายงานสถานะ Memory ทุกๆ 10 วินาที
}