// ============================================================================
// 🐾 SMART PET FEEDER - MASTER FIRMWARE (ESP32 30-PIN)
// ============================================================================

// ==========================================
// 🔴 หมวดที่ 0: MASTER DEBUG SWITCH (สวิตช์เปิด/ปิด ข้อความหลังบ้าน)
// ==========================================
// 💡 เปลี่ยนเป็น 1 = เปิดข้อความ Debug, เปลี่ยนเป็น 0 = ปิดข้อความทั้งหมดเพื่อประหยัด RAM
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

// ==========================================
// 📚 หมวดที่ 1: LIBRARIES (เรียกใช้ไลบรารี)
// ==========================================
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoWebsockets.h>
#include "time.h"
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <HardwareSerial.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "RTClib.h"
#include <ESP32Servo.h>
#include <VL53L0X.h>
#include <HX711_ADC.h>

using namespace websockets;

// ==========================================
// 🔌 หมวดที่ 2: PIN DEFINITIONS (กำหนดขาอุปกรณ์)
// ==========================================
#define SERVO_PIN 13  
#define PUMP_PIN 12   

#define HX_BowlWater_DT 25  
#define HX_BowlWater_SCK 26
#define HX_TankWater_DT 27  
#define HX_TankWater_SCK 14
#define HX_BowlFood_DT 32  
#define HX_BowlFood_SCK 33

// ==========================================
// ⚙️ หมวดที่ 3: CONSTANTS & SETTINGS (ตั้งค่าระบบ)
// ==========================================
const int EEPROM_SIZE = 512;
const int ADDR_LIMIT_FOOD = 0;    
const int ADDR_LIMIT_WATER = 10;  
const int ADDR_SCHEDULES = 20;    

const int SPEED_FWD = 180;                    
const int SPEED_STOP = 90;                    
const unsigned long FEED_TIMEOUT_MS = 40000;  
const unsigned long SERVO_STOP_DELAY = 100;   

const unsigned long WATER_REFILL_DELAY_MS = 60000;  
const int WATER_DETECT_GAP = 50;                    
const unsigned long WATER_TIMEOUT_MS = 40000;       

const int TANK_EMPTY = 450;  
const int TANK_FULL = 50;    

const unsigned long TANK_CHECK_INTERVAL_MS = 5000;    
const unsigned long CAM_SYNC_INTERVAL = 60000;         
const unsigned long DELAY_SCHEDUIE = 1000;             
const unsigned long WEBSOCKET_SEND_INTERVAL = 3000;    
const unsigned long WEBSOCKET_RETRY_INTERVAL = 10000;  
const unsigned long END_DELAY_NET_CHECK = 60000;
const unsigned long END_DELAY_WIFI_RETRY = 30000;
const unsigned long END_DELAY_SCREEN = 1000;
const unsigned long resetDelay = 300;  

const int X_MIN = 450;
const int X_MAX = 3900;
const int Y_MIN = 150;
const int Y_MAX = 3800;
const int PLUS_MINUS = 200;

const uint16_t TFT_BABYBLUE = 0xB6FF;
const uint16_t TFT_CREAMYELLOW = 0xFFF2;
const uint16_t TFT_LIGHTGREEN = 0x9772;

String currentFeedSource = "";  
int currentFeedAmount = 0;      

// ==========================================
// 📦 หมวดที่ 4: DATA TYPES (โครงสร้างสถานะต่างๆ)
// ==========================================
enum FeedMode {
  FILL_UP_TO,  
  ADD_MORE     
};

enum FeederState {
  IDLE,
  FORWARD,
  REVERSE,
  FINISH
};

enum WaterState {
  WATER_IDLE,
  WATER_WAITING,
  WATER_PUMPING
};

struct FeedingTime {
  int hour = 0;         
  int minute = 0;       
  int gram = 10;        
  bool active = false;  
};

// ==========================================
// 🛠️ หมวดที่ 5: OBJECT INSTANTIATIONS (สร้างอ็อบเจกต์)
// ==========================================
HardwareSerial CamSerial(2);  
WebsocketsClient client;
WiFiManager wm;
RTC_DS3231 rtc;
Servo feedServo;
VL53L0X tankSensor;
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS);  

HX711_ADC LoadCell_BowlWater(HX_BowlWater_DT, HX_BowlWater_SCK);
HX711_ADC LoadCell_TankWater(HX_TankWater_DT, HX_TankWater_SCK);
HX711_ADC LoadCell_BowlFood(HX_BowlFood_DT, HX_BowlFood_SCK);

// ==========================================
// 💾 หมวดที่ 6: GLOBAL VARIABLES (ตัวแปรส่วนกลาง)
// ==========================================
const char* websocket_server_host = "34.45.167.7";  
const uint16_t server_port = 4000;                  
const char* myToken = "ESP32-CAM-001";    
const char* deviceRole = "main";          
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  
const int daylightOffset_sec = 0;

unsigned long lastWebSocketSend = 0;
unsigned long lastCamSync = 0;
unsigned long startDelayNetCheck = 0;
unsigned long startDelayWifiRetry = 0;
bool wifiStatus = false;
bool lastWifiStatus = false;
bool ntpStarted = false;

bool maintenanceMode = false;  
int actionButton = 0;
unsigned long actionStartTime = 0;
unsigned long startDelayScreen = 0;
String lastTimeStr = "";
int currentPage = 0;
bool checkRtc = false;
bool rtcBoot = false;

FeedingTime schedules[3];  
int editIdx = 0;           
int confirmMode = 0;       
int deleteIdx = 0;         

int limitBowlFood = 100;  
int bowlFood = 0;         
int tankFood = 0;         
int manualFeedAmount = 1;
float feedTargetWeight = 0;  
bool petAteTrigger = false;
bool forceUpdateTank = false;

FeederState feedState = IDLE;
unsigned long feedTimer = 0;
unsigned long stopTimer = 0;

int limitBowlWater = 100;  
int bowlWater = 0;         
int tankWater = 0;         
int lastDrinkWeight = 0;   
int tempFeedAmount = 10;
const int waterTankMax = 2000;  
const int waterTankMin = 0;     

WaterState waterState = WATER_IDLE;
unsigned long pumpTimer = 0;
unsigned long waterWaitTimer = 0;


// ============================================================================
// ======================= 🔧 หมวดที่ 7: UTILITIES & EEPROM =====================
// ============================================================================
bool hasInternet() {
  WiFiClient client;
  return client.connect("8.8.8.8", 53);
}

void saveSettings() {
  EEPROM.put(ADDR_LIMIT_FOOD, limitBowlFood);
  EEPROM.put(ADDR_LIMIT_WATER, limitBowlWater);
  EEPROM.put(ADDR_SCHEDULES, schedules);
  EEPROM.commit();
  DEBUG_PRINTLN(">>> Settings Saved to EEPROM");
}

void loadSettings() {
  EEPROM.get(ADDR_LIMIT_FOOD, limitBowlFood);
  EEPROM.get(ADDR_LIMIT_WATER, limitBowlWater);
  EEPROM.get(ADDR_SCHEDULES, schedules);

  if (limitBowlFood < 50 || limitBowlFood > 5000 || limitBowlFood % 100 != 0) {
    limitBowlFood = 100;
    DEBUG_PRINTLN(">>> Invalid Food Limit! Reset to 100.");
  }

  if (limitBowlWater < 50 || limitBowlWater > 5000 || limitBowlWater % 100 != 0) {
    limitBowlWater = 100;
    DEBUG_PRINTLN(">>> Invalid Water Limit! Reset to 100.");
  }

  for (int i = 0; i < 3; i++) {
    bool timeInvalid = (schedules[i].hour > 23 || schedules[i].minute > 59);
    bool gramInvalid = (schedules[i].gram <= 0 || schedules[i].gram > 500 || schedules[i].gram % 10 != 0);

    if (timeInvalid || gramInvalid) {
      schedules[i].hour = 0;
      schedules[i].minute = 0;
      schedules[i].gram = 10;
      schedules[i].active = false;
      DEBUG_PRINTF(">>> Slot %d corrupted! Resetting to default.\n", i + 1);
    }
  }
  DEBUG_PRINTLN(">>> Settings Loaded & Filtered");
}

// ============================================================================
// =================== ⚖️ หมวดที่ 8: HARDWARE & SENSORS CONFIG ==================
// ============================================================================
void rtcStart() {
  if (!rtc.begin()) {
    checkRtc = false;
  } else {
    checkRtc = true;
    if (rtc.lostPower()) {
      DEBUG_PRINTLN("RTC lost power, let's set the time!");
    }
  }
}

void syncSystemTimeFromRtc() {
  if (!checkRtc) return;
  DateTime now = rtc.now();

  struct tm timeinfo;
  timeinfo.tm_year = now.year() - 1900;
  timeinfo.tm_mon = now.month() - 1;
  timeinfo.tm_mday = now.day();
  timeinfo.tm_hour = now.hour();
  timeinfo.tm_min = now.minute();
  timeinfo.tm_sec = now.second();

  struct timeval tv;
  tv.tv_sec = mktime(&timeinfo);
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  DEBUG_PRINTLN(">>> System Time Synced from RTC (No WiFi needed)");
}

void vl53l0xFood() {
  tankSensor.setTimeout(500);
  if (tankSensor.init()) {
    tankSensor.stopContinuous();
  }
}

void loadCellBowlFood() {
  LoadCell_BowlFood.begin();
  LoadCell_BowlFood.start(2000, false);
  LoadCell_BowlFood.setTareOffset(8921119);
  LoadCell_BowlFood.setCalFactor(420.0);
  LoadCell_BowlFood.setSamplesInUse(32);
}

void loadCellBowlWater() {
  LoadCell_BowlWater.begin();
  LoadCell_BowlWater.start(2000, false);
  LoadCell_BowlWater.setTareOffset(8935986);
  LoadCell_BowlWater.setCalFactor(420.0);
  LoadCell_BowlWater.setSamplesInUse(32);
}

void loadCellTankWater() {
  LoadCell_TankWater.begin();
  LoadCell_TankWater.start(2000, false);
  LoadCell_TankWater.setTareOffset(8045716);
  LoadCell_TankWater.setCalFactor(420.0);
  LoadCell_TankWater.setSamplesInUse(32);
}

void updateTankLevel() {
  DEBUG_PRINTLN(">>> Checking Tank Level (Advanced Filter)...");
  tankSensor.startContinuous();
  long totalDistance = 0;
  int validReadings = 0;

  for (int i = 0; i < 10; i++) {
    int distance = tankSensor.readRangeContinuousMillimeters();
    if (distance > 10 && distance < 8000 && distance < (TANK_EMPTY + 100)) {
      totalDistance += distance;
      validReadings++;
    }
    delay(10);
  }
  tankSensor.stopContinuous();

  if (validReadings > 0) {
    int currentDistance = totalDistance / validReadings;
    static int smoothDistance = -1;
    if (smoothDistance == -1) {
      smoothDistance = currentDistance;
    } else {
      smoothDistance = (smoothDistance * 0.7) + (currentDistance * 0.3);
    }
    DEBUG_PRINT("Raw: ");
    DEBUG_PRINT(currentDistance);
    DEBUG_PRINT("mm | Smooth: ");
    DEBUG_PRINT(smoothDistance);
    DEBUG_PRINTLN("mm");

    int percent = map(smoothDistance, TANK_EMPTY, TANK_FULL, 0, 100);
    tankFood = constrain(percent, 0, 100);
    DEBUG_PRINT("Final Food: ");
    DEBUG_PRINT(tankFood);
    DEBUG_PRINTLN("%");
  } else {
    DEBUG_PRINTLN("Error: Sensor blocked or out of range!");
  }
}

void loadCellBowlFoodWork(unsigned long now) {
  if (LoadCell_BowlFood.update()) {
    bowlFood = (int)LoadCell_BowlFood.getData();
    if (bowlFood < 0) bowlFood = 0;
  }
}

void vl53l0xFoodWork(unsigned long now) {
  static unsigned long lastPeriodicCheck = 0;
  if ((now - lastPeriodicCheck > TANK_CHECK_INTERVAL_MS) || forceUpdateTank) {
    updateTankLevel();
    forceUpdateTank = false;
    lastPeriodicCheck = now;
    DEBUG_PRINTLN(">>> Tank Level Updated (Dispensed/Timer/Manual)");
  }
}

void loadCellWaterBowlWork() {
  if (LoadCell_BowlWater.update()) {
    bowlWater = (int)LoadCell_BowlWater.getData();
    if (bowlWater < 0) bowlWater = 0;
  }
}

void loadCellWaterTankWork() {
  if (LoadCell_TankWater.update()) {
    int currentTankWeight = (int)LoadCell_TankWater.getData();
    if (currentTankWeight < 0) currentTankWeight = 0;
    int percent = map(currentTankWeight, waterTankMin, waterTankMax, 0, 100);
    tankWater = constrain(percent, 0, 100);
  }
}

// ============================================================================
// ==================== 🌐 หมวดที่ 9: NETWORK & WEBSOCKET =======================
// ============================================================================

void resetWiFi() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(40, 100);
  tft.println("RESETTING WIFI...");
  delay(1000);

  DEBUG_PRINTLN(">>> Graceful Shutdown Started...");

  // 🌟 [จุดสำคัญ!] สั่งหารให้บอร์ดกล้องรีบูทตามไปพร้อมกัน จะได้ไม่ค้างคาเน็ตตัวเก่า!
  CamSerial.println("REBOOT_CAM");
  delay(500);

  client.close();
  WiFi.disconnect(true, true);
  delay(500);  

  digitalWrite(PUMP_PIN, LOW);
  feedServo.detach();

  pinMode(HX_BowlWater_SCK, OUTPUT);
  digitalWrite(HX_BowlWater_SCK, HIGH);
  pinMode(HX_TankWater_SCK, OUTPUT);
  digitalWrite(HX_TankWater_SCK, HIGH);
  pinMode(HX_BowlFood_SCK, OUTPUT);
  digitalWrite(HX_BowlFood_SCK, HIGH);
  delay(100);

  wm.resetSettings();
  delay(500);  

  DEBUG_PRINTLN(">>> Rebooting ESP32...");
  ESP.restart();
}

void configModeCallback(WiFiManager* myWiFiManager) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(60, 30);
  tft.setTextSize(3);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.println("CONFIG MODE");
  tft.setCursor(20, 80);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("Wifi:");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.println(myWiFiManager->getConfigPortalSSID());
  tft.setCursor(20, 140);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("BrowserIP:");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println(WiFi.softAPIP());
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(120);

  bool connected = wm.autoConnect("Smart Pet Feeder");

  if (connected) {
    DEBUG_PRINTLN("✅ WiFi Connected");
    DEBUG_PRINT("IP Address: ");
    DEBUG_PRINTLN(WiFi.localIP());

    client.close();
    delay(500);

    wifiStatus = hasInternet();

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntpStarted = true;

    delay(1000);
    sendWifiToCam();

    forceUpdateTank = true;
  } else {
    DEBUG_PRINTLN("❌ WiFi Connect Failed");
    ESP.restart();
  }
}

void syncRtcFromNtp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    rtcBoot = true;
  }
}

void checkInternetConnection(unsigned long currentTime) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpStarted) {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      ntpStarted = true;
    }
    if (currentTime - startDelayNetCheck >= END_DELAY_NET_CHECK) {
      startDelayNetCheck = currentTime;
      wifiStatus = hasInternet();
      if (wifiStatus && checkRtc && !rtcBoot) {
        syncRtcFromNtp();
      }
    }
  } else {
    wifiStatus = false;
    rtcBoot = false;
    ntpStarted = false;
    if (currentTime - startDelayWifiRetry >= END_DELAY_WIFI_RETRY) {
      startDelayWifiRetry = currentTime;
      if (WiFi.status() == WL_IDLE_STATUS || WiFi.status() == WL_DISCONNECTED) {
        WiFi.begin();
      }
    }
  }
}

void checkStatusWifi() {
  if (currentPage != 0) return;
  if (wifiStatus != lastWifiStatus) {
    int x = 300;
    int y = 18;
    if (wifiStatus) {
      tft.fillCircle(x, y, 3, TFT_GREEN);
      tft.drawCircle(x, y, 8, TFT_GREEN);
      tft.drawCircle(x, y, 9, TFT_GREEN);
      tft.drawCircle(x, y, 14, TFT_GREEN);
      tft.drawCircle(x, y, 15, TFT_GREEN);
    } else {
      tft.fillCircle(x, y, 3, TFT_RED);
      tft.drawCircle(x, y, 8, TFT_RED);
      tft.drawCircle(x, y, 9, TFT_RED);
      tft.drawCircle(x, y, 14, TFT_RED);
      tft.drawCircle(x, y, 15, TFT_RED);
    }
    lastWifiStatus = wifiStatus;
  }
}

void parseScheduleFromWebSocket(String raw) {
  for (int i = 0; i < 3; i++) {
    schedules[i].active = false;
    schedules[i].hour = 0;
    schedules[i].minute = 0;
    schedules[i].gram = 10;
  }

  int start = 0;
  int slot = 0;
  while (start < raw.length() && slot < 3) {
    int end = raw.indexOf(';', start);
    if (end == -1) end = raw.length();

    String item = raw.substring(start, end);
    int firstColon = item.indexOf(':');
    int secondColon = item.lastIndexOf(':');

    if (firstColon > 0 && secondColon > firstColon) {
      schedules[slot].hour = item.substring(0, firstColon).toInt();
      schedules[slot].minute = item.substring(firstColon + 1, secondColon).toInt();
      schedules[slot].gram = item.substring(secondColon + 1).toInt();
      schedules[slot].active = true;
    }
    slot++;
    start = end + 1;
  }
  saveSettings();
  DEBUG_PRINTLN("✅ WebSocket: Schedules Updated");
}

void onMessageCallback(WebsocketsMessage message) {
  String data = message.data();
  DEBUG_PRINTLN("📥 RAW WSc Data: " + data);

  if (data.startsWith("{")) {

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, data);

    if (error) {
      DEBUG_PRINT("❌ JSON Parse Failed: ");
      DEBUG_PRINTLN(error.f_str());
      return;
    }

    const char* type = doc["type"];

    if (!type) return;

    if (strcmp(type, "manual_feed") == 0) {
      int feedGram = doc["amount"];
      if (feedGram > 0) {
        DEBUG_PRINTF("🌐 Web requested manual feed: %dg\n", feedGram);
        startFeeding(feedGram, ADD_MORE, millis());
      } else {
        DEBUG_PRINTLN("⚠️ Warning: manual_feed amount is 0 or invalid!");
      }
    } else if (strcmp(type, "schedule_update") == 0) {
      const char* rawSchedule = doc["raw"];
      if (rawSchedule) {
        DEBUG_PRINTLN("📥 Received Schedule: " + String(rawSchedule));
        parseScheduleFromWebSocket(String(rawSchedule));

        if (WiFi.status() == WL_CONNECTED) {
          String ackJson = "{\"type\":\"ack\", \"role\":\"main\", \"token\":\"" + String(myToken) + "\", \"msg\":\"schedule_updated\"}";
          client.send(ackJson);
          DEBUG_PRINTLN("📤 Sent ACK to Server: " + ackJson);
        }
      }
    }

  } else {
    DEBUG_PRINTLN("⚠️ Ignored Non-JSON Message");
  }
}

void handleWebSocket(unsigned long now) {
  static unsigned long lastWsConnect = 0;

  if (WiFi.status() != WL_CONNECTED) {
    client.close();
    return;
  }

  client.poll();

  if (!client.available()) {
    if (now - lastWsConnect > WEBSOCKET_RETRY_INTERVAL) {
      lastWsConnect = now;

      DEBUG_PRINTLN("Connecting to WebSocket...");

      client.close();
      delay(300);

      bool connected = client.connect(websocket_server_host, server_port, "/");

      if (connected) {
        DEBUG_PRINTLN("✅ WebSocket Connected!");

        client.onMessage(onMessageCallback);

        client.send("{\"type\":\"register\", \"role\":\"main\", \"token\":\"" + String(myToken) + "\"}");
      } else {
        DEBUG_PRINTLN("❌ WebSocket Connect Failed");
      }
    }
    return;
  }

  if (now - lastWebSocketSend > WEBSOCKET_SEND_INTERVAL) {
    lastWebSocketSend = now;

    String json = "{\"type\":\"update_sensor\", \"role\":\"main\", \"token\":\"" + String(myToken) + "\", \"food\":" + String(tankFood) + ", \"water\":" + String(tankWater) + ", \"bowlFood\":" + String(bowlFood) + ", \"bowlWater\":" + String(bowlWater) + "}";

    client.send(json);
  }
}

void sendWifiToCam() {
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = wm.getWiFiSSID(true);
    String pass = wm.getWiFiPass(true);

    if (ssid.length() == 0) {
      DEBUG_PRINTLN("⚠️ SSID empty, cannot send WiFi to CAM");
      return;
    }

    String dataPacket = ssid + "," + pass + "\n";

    CamSerial.print(dataPacket);
    delay(100);
    CamSerial.print(dataPacket);

    DEBUG_PRINTLN("📤 Sent WiFi to CAM SSID: " + ssid);
  }
}

void handleCameraSync(unsigned long now) {
  if (now - lastCamSync >= CAM_SYNC_INTERVAL) {
    lastCamSync = now;
    sendWifiToCam();
  }
}

// ============================================================================
// ======================= 🧠 หมวดที่ 10: CORE LOGIC & PROCESS ==================
// ============================================================================

void checkESP32_RAM(unsigned long now) {
  static unsigned long lastRamCheck = 0;

  if (now - lastRamCheck > 10000) {
    lastRamCheck = now;

    uint32_t freeRam = ESP.getFreeHeap();
    uint32_t totalRam = ESP.getHeapSize();
    uint8_t ramPercent = (freeRam * 100) / totalRam;

    DEBUG_PRINTF("🧠 [SYSTEM] Free RAM: %d bytes (%d%% free) | Min Free Ever: %d bytes\n",
                 freeRam, ramPercent, ESP.getMinFreeHeap());
  }
}

void startFeeding(int value, FeedMode mode, unsigned long now) {
  if (maintenanceMode || tankFood <= 0) {
    DEBUG_PRINTLN("!!! BLOCKED: Maintenance Mode ON or Food Tank is EMPTY !!!");
    return;
  }

  float currentWeight = bowlFood;

  if (feedState != IDLE) return;

  if (currentWeight >= limitBowlFood) {
    DEBUG_PRINTLN("!!! Bowl is FULL. Cannot feed. !!!");
    return;
  }

  float calculatedTarget = 0;
  if (mode == FILL_UP_TO) {
    calculatedTarget = value;
  } else {
    calculatedTarget = currentWeight + value;
  }

  if (calculatedTarget > limitBowlFood) {
    DEBUG_PRINTF("!!! Warning: Target %0.2fg exceeds capacity. Capping at %dg.\n", calculatedTarget, limitBowlFood);
    feedTargetWeight = limitBowlFood;
  } else {
    feedTargetWeight = calculatedTarget;
  }

  if (currentWeight >= feedTargetWeight) {
    DEBUG_PRINTLN(">>> Target already reached. Skipping.");
    return;
  }

  DEBUG_PRINTF(">>> Feeding Started. Target: %0.2fg\n", feedTargetWeight);

  currentFeedAmount = value;
  if (mode == FILL_UP_TO) {
    currentFeedSource = "schedule";
  } else {
    currentFeedSource = "manual";
  }

  feedServo.attach(SERVO_PIN);
  feedServo.write(SPEED_FWD);
  feedState = FORWARD;
  feedTimer = now;
}

void processFeeder(unsigned long now) {
  if (feedState == IDLE) return;

  LoadCell_BowlFood.update();
  float currentWeight = LoadCell_BowlFood.getData();
  bowlFood = (int)currentWeight;

  static unsigned long targetReachedTime = 0;
  unsigned long realNow = millis();

  switch (feedState) {
    case FORWARD:
      if (realNow - feedTimer > FEED_TIMEOUT_MS) {
        DEBUG_PRINTLN("!!! Timeout !!!");
        feedServo.write(SPEED_STOP);
        stopTimer = realNow;
        feedState = FINISH;
        targetReachedTime = 0;
        break;
      }

      if (currentWeight >= feedTargetWeight) {
        if (targetReachedTime == 0) {
          targetReachedTime = realNow;
        } else if (realNow - targetReachedTime > 1000) {  
          DEBUG_PRINTLN(">>> Target Reached and Stable!");
          feedServo.write(SPEED_STOP);
          stopTimer = realNow;
          feedState = FINISH;
          targetReachedTime = 0;
        }
      } else {
        targetReachedTime = 0;
      }
      break;

    case FINISH:
      if (realNow - stopTimer > SERVO_STOP_DELAY) {
        feedServo.detach();
        DEBUG_PRINTLN(">>> Feed Complete (Async)");
        forceUpdateTank = true;
        feedState = IDLE;

        if (WiFi.status() == WL_CONNECTED) {
          String logJson = "{\"type\":\"feed_log\", \"role\":\"main\", \"token\":\"" + String(myToken) + "\", \"amount\":" + String(currentFeedAmount) + ", \"source\":\"" + currentFeedSource + "\"}";
          client.send(logJson);
          DEBUG_PRINTLN("📤 Sent Feed Log to Server: " + logJson);
        }
      }
      break;
  }
}

void processWater(unsigned long now) {
  if (maintenanceMode || tankWater <= 0) {
    if (waterState != WATER_IDLE) {
      digitalWrite(PUMP_PIN, LOW);
      waterState = WATER_IDLE;
      DEBUG_PRINTLN("!!! System LOCKED or Tank Empty -> Water System Reset & Disabled !!!");
    }
    return;
  }

  switch (waterState) {
    case WATER_IDLE:
      if (bowlWater <= limitBowlWater - WATER_DETECT_GAP) {
        DEBUG_PRINTLN(">>> Pet drank water.");
        waterState = WATER_WAITING;
        waterWaitTimer = now;
        lastDrinkWeight = bowlWater;
      }
      break;

    case WATER_WAITING:
      if (LoadCell_BowlWater.getData() < -15.0) {
        waterWaitTimer = now;
        DEBUG_PRINTLN("!!! Water Bowl Missing! Pump Paused. !!!");
      } else if (bowlWater < lastDrinkWeight - 5) {  
        waterWaitTimer = now;
        lastDrinkWeight = bowlWater;
        DEBUG_PRINTLN(">>> Pet is still drinking. Timer reset.");
      } else if (bowlWater >= limitBowlWater) {
        waterState = WATER_IDLE;
        DEBUG_PRINTLN(">>> Water is already full. Cancel countdown.");
      } else if (now - waterWaitTimer >= WATER_REFILL_DELAY_MS) {
        DEBUG_PRINTLN(">>> Starting Pump...");
        digitalWrite(PUMP_PIN, HIGH);
        pumpTimer = now;
        waterState = WATER_PUMPING;
      }
      break;

    case WATER_PUMPING:
      if (bowlWater >= (limitBowlWater - 40)) {
        DEBUG_PRINTLN(">>> Water Refilled to Limit. Stop Pump.");
        digitalWrite(PUMP_PIN, LOW);
        waterState = WATER_IDLE;

        tft.init();
        tft.setRotation(1);
        ts.begin();

        switch (currentPage) {
          case 0: drawHomePage(); break;
          case 1: drawMenuPage(); break;
          case 2:
            if (confirmMode == 1) {
              drawConfirmPage("Confirm Reset", "WiFi?", 1);
            } else if (confirmMode == 2) {
              drawConfirmPage("Confirm Delete", "Round: " + String(deleteIdx + 1), 2);
            }
            break;
          case 3: drawSetTimeFeedPage(); break;
          case 4: drawSetTime(); break;
          case 5: drawCheckSetTime(); break;
          case 6: drawSetLimitPage(); break;
          case 7: drawSetUpPage(); break;
          default: drawHomePage(); break;
        }

      } else if (now - pumpTimer > WATER_TIMEOUT_MS) {
        DEBUG_PRINTLN("!!! Water Pump Timeout! Force Stop. !!!");
        digitalWrite(PUMP_PIN, LOW);
        waterState = WATER_IDLE;
      }
      break;
  }
}

void processSchedule(unsigned long now) {
  static unsigned long lastCheckTime = 0;
  if (now - lastCheckTime < DELAY_SCHEDUIE) return;
  lastCheckTime = now;

  int currentHour = -1;
  int currentMinute = -1;
  int currentSecond = -1;

  if (wifiStatus) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      currentHour = timeinfo.tm_hour;
      currentMinute = timeinfo.tm_min;
      currentSecond = timeinfo.tm_sec;
    }
  }

  if (currentHour == -1 && checkRtc) {
    DateTime dt = rtc.now();
    if (dt.isValid()) {
      currentHour = dt.hour();
      currentMinute = dt.minute();
      currentSecond = dt.second();
    }
  }

  if (currentHour == -1) return;

  for (int i = 0; i < 3; i++) {
    if (schedules[i].active) {
      if (currentHour == schedules[i].hour && currentMinute == schedules[i].minute && currentSecond < 2) {
        DEBUG_PRINTF(">>> Schedule #%d Triggered at %02d:%02d (%dg)\n", i + 1, currentHour, currentMinute, schedules[i].gram);
        startFeeding(schedules[i].gram, FILL_UP_TO, now);
      }
    }
  }
}


// ============================================================================
// ======================= 🎨 หมวดที่ 11: UI & DISPLAY GRAPHICS ===============
// ============================================================================
void drawHomePage() {
  currentPage = 0;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 320, 40, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Smart Pet Feeder");

  tft.drawRect(10, 50, 300, 30, TFT_WHITE);

  tft.fillRect(15, 90, 140, 40, TFT_BABYBLUE);
  tft.drawRect(15, 90, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_BABYBLUE);
  tft.setCursor(31, 102);
  tft.print("Food:");
  tft.print(tankFood);
  tft.print("%");

  tft.fillRect(15, 140, 140, 40, TFT_BABYBLUE);
  tft.drawRect(15, 140, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_BABYBLUE);
  tft.setCursor(20, 152);
  tft.print("Bowl:");
  tft.print(bowlFood);
  tft.print("g.");

  tft.fillRect(15, 190, 140, 40, TFT_DARKGREEN);
  tft.drawRect(15, 190, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setCursor(37, 202);
  tft.print("FEEDFOOD");

  tft.fillRect(165, 90, 140, 40, TFT_CREAMYELLOW);
  tft.drawRect(165, 90, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_CREAMYELLOW);
  tft.setCursor(175, 102);
  tft.print("Water:");
  tft.print(tankWater);
  tft.print("%");

  tft.fillRect(165, 140, 140, 40, TFT_CREAMYELLOW);
  tft.drawRect(165, 140, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_CREAMYELLOW);
  tft.setCursor(170, 152);
  tft.print("Bowl:");
  tft.print(bowlWater);
  tft.print("mL");

  tft.fillRect(165, 190, 140, 40, TFT_BLUE);
  tft.drawRect(165, 190, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(211, 202);
  tft.print("MENU");

  lastTimeStr = "";
  lastWifiStatus = !wifiStatus;
  forceUpdateTank = true;
}

void drawMenuPage() {
  currentPage = 1;
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  tft.fillRect(0, 0, 320, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
  tft.setCursor(10, 10);
  tft.print("Menu");

  tft.fillRect(10, 50, 300, 40, TFT_RED);
  tft.drawRect(10, 50, 300, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setCursor(100, 62);
  tft.print("RESET WiFi");

  tft.fillRect(10, 100, 140, 40, TFT_BLACK);
  tft.drawRect(10, 100, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(32, 112);
  tft.print("Set Time");

  tft.fillRect(10, 150, 140, 40, TFT_BLACK);
  tft.drawRect(10, 150, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(20, 162);
  tft.print("Check Time");

  tft.fillRect(170, 100, 140, 40, TFT_BLACK);
  tft.drawRect(170, 100, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(186, 112);
  tft.print("Set Limit");

  tft.fillRect(170, 150, 140, 40, TFT_BLACK);
  tft.drawRect(170, 150, 140, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(204, 162);
  tft.print("Set Up");

  backButton();
}

void drawConfirmPage(String title, String subTitle, int num) {
  currentPage = 2;
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(40, 40, 240, 160, TFT_WHITE);
  if (num == 1) {
    tft.setTextColor(TFT_RED);
    tft.setTextSize(3);
    tft.setCursor(43, 70);
    tft.print(title);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(130, 100);
    tft.print(subTitle);
  } else {
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(76, 70);
    tft.print(title);

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(110, 100);
    tft.print(subTitle);
  }

  tft.fillRect(55, 130, 100, 50, TFT_RED);
  tft.drawRect(55, 130, 100, 50, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setCursor(87, 147);
  tft.print("YES");

  tft.fillRect(165, 130, 100, 50, TFT_NAVY);
  tft.drawRect(165, 130, 100, 50, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(203, 147);
  tft.print("NO");
}

void drawSetTimeFeedPage() {
  currentPage = 3;
  tempFeedAmount = 10;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(60, 20, 200, 50, TFT_BLACK);
  tft.drawRect(60, 20, 200, 50, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(154, 37);
  tft.print("+");

  tft.fillRect(60, 180, 200, 50, TFT_BLACK);
  tft.drawRect(60, 180, 200, 50, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(154, 197);
  tft.print("-");

  tft.drawRect(60, 80, 200, 90, TFT_WHITE);

  tft.setTextSize(2);
  tft.setCursor(112, 90);
  tft.print("FEEDFOOD");

  updateTimerFeedValue(tempFeedAmount);

  tft.fillRect(70, 135, 80, 30, TFT_DARKGREEN);
  tft.drawRect(70, 135, 80, 30, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(92, 142);
  tft.print("YES");

  tft.fillRect(170, 135, 80, 30, TFT_RED);
  tft.drawRect(170, 135, 80, 30, TFT_WHITE);
  tft.setCursor(198, 142);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.print("NO");
}

void updateSetTimeValues() {
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  int vals[] = { schedules[editIdx].hour, schedules[editIdx].minute, editIdx + 1, schedules[editIdx].gram };
  const char* labels[] = { "Hour", "Min", "Round", "Gram" };

  for (int i = 0; i < 4; i++) {
    int x = 10 + (i * 78);
    tft.fillRect(x + 1, 91, 66, 58, TFT_BLACK);
    if (i == 1) tft.setCursor(x + 16, 100);
    else if (i == 2) tft.setCursor(x + 4, 100);
    else tft.setCursor(x + 10, 100);

    tft.print(labels[i]);

    if (i == 3) {
      if (vals[i] < 100) {
        tft.setCursor(x + 16, 120);
      } else {
        tft.setCursor(x + 6, 120);
      }
      tft.print(vals[i]);
      tft.print("g.");
    } else if (i == 2) {
      tft.setCursor(x + 27, 120);
      tft.print(vals[i]);
    } else {
      tft.setCursor(x + 22, 120);
      tft.printf("%02d", vals[i]);
    }
  }
}

void drawSetTime() {
  currentPage = 4;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 320, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Set Time");
  tft.setTextColor(TFT_WHITE);

  for (int i = 0; i < 4; i++) {
    int x = 10 + (i * 78);
    tft.drawRect(x, 50, 68, 30, TFT_WHITE);
    tft.setCursor(x + 28, 57);
    tft.print("+");

    tft.drawRect(x, 90, 68, 60, TFT_WHITE);

    tft.drawRect(x, 160, 68, 30, TFT_WHITE);
    tft.setCursor(x + 28, 167);
    tft.print("-");
  }

  tft.fillRect(0, 200, 160, 40, TFT_DARKGREEN);
  tft.drawRect(0, 200, 160, 40, TFT_WHITE);
  tft.setCursor(62, 212);
  tft.print("YES");

  tft.fillRect(160, 200, 160, 40, TFT_RED);
  tft.drawRect(160, 200, 160, 40, TFT_WHITE);
  tft.setCursor(228, 212);
  tft.print("NO");

  updateSetTimeValues();
}

void drawCheckSetTime() {
  currentPage = 5;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 320, 40, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Feeding Schedule");

  const char* top[] = { "R", "Time", "Gram", "Status" };
  tft.setTextColor(TFT_YELLOW);
  for (int i = 0; i < 4; i++) {
    int x = 10 + (i * 39);
    if (i == 2) tft.setCursor(x + 48, 55);
    else if (i == 3) tft.setCursor(x + 84, 55);
    else tft.setCursor(x, 55);
    tft.print(top[i]);
  }

  tft.drawLine(10, 75, 310, 75, TFT_WHITE);

  for (int i = 0; i < 3; i++) {
    int yPos = 90 + (i * 40);
    int y = 80 + (i * 40);
    int yDEL = 87 + (i * 40);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);

    tft.setCursor(10, yPos);
    tft.print(i + 1);

    tft.setCursor(49, yPos);
    tft.printf("%02d:%02d", schedules[i].hour, schedules[i].minute);

    if (schedules[i].gram < 100) {
      tft.setCursor(142, yPos);
    } else {
      tft.setCursor(132, yPos);
    }
    tft.print(schedules[i].gram);
    tft.print("g.");

    if (schedules[i].active) {
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(211, yPos);
      tft.print("ON");
    } else {
      tft.setTextColor(TFT_RED);
      tft.setCursor(211, yPos);
      tft.print("OFF");
    }
    tft.fillRect(270, y, 40, 30, TFT_RED);
    tft.drawRect(270, y, 40, 30, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setCursor(272, yDEL);
    tft.print("DEL");

    tft.drawLine(10, yPos + 25, 310, yPos + 25, TFT_DARKGREY);
  }

  backButton();
}

void drawSetLimitPage() {
  currentPage = 6;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 320, 40, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Set Bowl Limit");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(30, 50);
  tft.print("FOOD (g)");

  tft.drawRect(30, 76, 100, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(74, 88);
  tft.print("+");

  if (limitBowlFood >= 1000) {
    tft.setCursor(56, 125);
    tft.print(limitBowlFood);
  } else if (limitBowlFood >= 100) {
    tft.setCursor(62, 125);
    tft.print(limitBowlFood);
  } else {
    tft.setCursor(68, 125);
    tft.print(limitBowlFood);
  }

  tft.drawRect(30, 150, 100, 40, TFT_WHITE);
  tft.setCursor(74, 162);
  tft.print("-");

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(190, 50);
  tft.print("WATER (ml)");

  tft.drawRect(190, 76, 100, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(234, 88);
  tft.print("+");

  if (limitBowlWater >= 1000) {
    tft.setCursor(216, 125);
    tft.print(limitBowlWater);
  } else if (limitBowlWater >= 100) {
    tft.setCursor(222, 125);
    tft.print(limitBowlWater);
  } else {
    tft.setCursor(228, 125);
    tft.print(limitBowlWater);
  }

  tft.drawRect(190, 150, 100, 40, TFT_WHITE);
  tft.setCursor(234, 162);
  tft.print("-");

  backButton();
}

void drawSetUpPage() {
  currentPage = 7;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 320, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Maintenance Mode");

  if (maintenanceMode) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(70, 99);
    tft.print("(Safe to Clean)");

    tft.setTextSize(3);
    tft.setCursor(43, 50);
    tft.print("SYSTEM LOCKED");

    tft.fillRect(60, 140, 200, 50, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.setCursor(106, 153);
    tft.print("UNLOCK");
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(46, 99);
    tft.print("(Not Safe to Clean)");

    tft.setTextSize(3);
    tft.setCursor(52, 50);
    tft.print("SYSTEM READY");

    tft.fillRect(60, 140, 200, 50, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(124, 153);
    tft.print("LOCK");
  }

  backButton();
}

void updateTimerFeedValue(int value) {
  tft.fillRect(100, 110, 120, 25, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  if (value < 100) tft.setCursor(136, 110);
  else tft.setCursor(124, 110);
  tft.print(value);
  tft.print("g.");
}

void updateLimitValues() {
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.fillRect(50, 125, 80, 20, TFT_BLACK);
  if (limitBowlFood >= 1000) tft.setCursor(56, 125);
  else if (limitBowlFood >= 100) tft.setCursor(62, 125);
  else tft.setCursor(68, 125);
  tft.print(limitBowlFood);

  tft.fillRect(210, 125, 80, 20, TFT_BLACK);
  if (limitBowlWater >= 1000) tft.setCursor(216, 125);
  else if (limitBowlWater >= 100) tft.setCursor(222, 125);
  else tft.setCursor(228, 125);
  tft.print(limitBowlWater);
}

void restoreLimitButton(int btnCode) {
  tft.setTextColor(TFT_BLACK);
  int x, y;
  String sym;

  if (btnCode == 60) {
    x = 30;
    y = 76;
    sym = "+";
  } else if (btnCode == 61) {
    x = 30;
    y = 150;
    sym = "-";
  } else if (btnCode == 62) {
    x = 190;
    y = 76;
    sym = "+";
  } else if (btnCode == 63) {
    x = 190;
    y = 150;
    sym = "-";
  } else return;

  tft.fillRect(x, y, 100, 40, TFT_BLACK);
  tft.drawRect(x, y, 100, 40, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  if (sym == "+") tft.setCursor(x + 44, y + 12);
  else tft.setCursor(x + 44, y + 12);
  tft.print(sym);
}

// 💡 รีเฟรชเฉพาะบางจุดของจอ เพื่อไม่ให้ภาพรวมกระพริบ
void updateHomeDynamic(unsigned long now) {
  if (currentPage != 0) return;
  if (now - startDelayScreen >= END_DELAY_SCREEN) {
    startDelayScreen = now;
    char timeBuffer[20];
    bool timeValid = false;
    if (wifiStatus) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        sprintf(timeBuffer, "%02d:%02d:%02d",
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec);
        timeValid = true;
      }
    } else if (checkRtc) {
      DateTime dt = rtc.now();
      if (dt.isValid()) {
        sprintf(timeBuffer, "%02d:%02d:%02d",
                dt.hour(),
                dt.minute(),
                dt.second());
        timeValid = true;
      }
    }
    if (!timeValid) sprintf(timeBuffer, "--:--:--");
    String currentTimeStr = String(timeBuffer);
    if (currentTimeStr != lastTimeStr) {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextSize(2);
      tft.setCursor(112, 57);
      tft.print(currentTimeStr);
      lastTimeStr = currentTimeStr;
    }
  }

  static int lastTankFood = -1;
  static int lastBowlFood = -1;

  if (tankFood != lastTankFood) {
    tft.fillRect(90, 102, 60, 25, TFT_BABYBLUE);
    tft.setTextColor(TFT_BLACK, TFT_BABYBLUE);
    tft.setTextSize(2);
    tft.setCursor(90, 102);
    tft.print(tankFood);
    tft.print("%");
    lastTankFood = tankFood;
  }

  if (bowlFood != lastBowlFood) {
    tft.fillRect(80, 152, 70, 25, TFT_BABYBLUE);
    tft.setTextColor(TFT_BLACK, TFT_BABYBLUE);
    tft.setTextSize(2);
    tft.setCursor(80, 152);
    tft.print(bowlFood);
    tft.print("g.");
    lastBowlFood = bowlFood;
  }

  static int lastTankWater = -1;
  static int lastBowlWater = -1;

  if (tankWater != lastTankWater) {
    tft.fillRect(245, 102, 55, 20, TFT_CREAMYELLOW);
    tft.setTextColor(TFT_BLACK, TFT_CREAMYELLOW);
    tft.setTextSize(2);
    tft.setCursor(245, 102);
    tft.print(tankWater);
    tft.print("%");
    lastTankWater = tankWater;
  }

  if (bowlWater != lastBowlWater) {
    tft.fillRect(230, 152, 65, 20, TFT_CREAMYELLOW);
    tft.setTextColor(TFT_BLACK, TFT_CREAMYELLOW);
    tft.setTextSize(2);
    tft.setCursor(230, 152);
    tft.print(bowlWater);
    tft.print("mL");
    lastBowlWater = bowlWater;
  }
}

void backButton() {
  tft.fillRect(0, 200, 320, 40, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(130, 210);
  tft.print("< BACK");
}

void backButtonEffect() {
  tft.fillRect(0, 200, 320, 40, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(130, 210);
  tft.print("< BACK");
}

void drawButtonEffect(int x, int y, String symbol) {
  tft.fillRect(x, y, 200, 50, TFT_BLACK);
  tft.drawRect(x, y, 200, 50, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);

  if (symbol == "+") tft.setCursor(x + 92, y + 13);
  else tft.setCursor(x + 92, y + 13);

  tft.print(symbol);
}

void setTimeEffectButton(int action) {
  if (action >= 40 && action <= 47) {
    int i = (action < 44) ? action - 40 : action - 44;
    int yPos = (action < 44) ? 50 : 160;
    const char* sym = (action < 44) ? "+" : "-";
    int xPos = 10 + (i * 78);

    tft.fillRect(xPos, yPos, 68, 30, TFT_BLACK);
    tft.drawRect(xPos, yPos, 68, 30, TFT_WHITE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(xPos + 28, yPos + 7);
    tft.print(sym);
  }
}


// ============================================================================
// ======================= 🖱️ หมวดที่ 12: TOUCH HANDLING ========================
// ============================================================================
void checkTouch(unsigned long now) {
  if (actionButton != 0) return;
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    // 💡 ปรับค่าการสัมผัสให้เข้ากับพิกัดจริงบนหน้าจอ
    int x = map(p.x, X_MAX, X_MIN, 0, tft.width());
    int y = map(p.y, Y_MAX, Y_MIN, 0, tft.height());
    x = constrain(x, 0, tft.width() - 1);
    y = constrain(y, 0, tft.height() - 1);

    if (currentPage == 0) {
      if (x > 15 && x < 155 && y > 190 && y < 230) {
        tft.fillRect(15, 190, 140, 40, TFT_LIGHTGREEN);
        tft.setTextColor(TFT_BLACK, TFT_LIGHTGREEN);
        tft.setCursor(37, 202);
        tft.print("FEEDFOOD");
        actionButton = 1;
        actionStartTime = now;
      } else if (x > 165 && x < 305 && y > 190 && y < 230) {
        tft.fillRect(165, 190, 140, 40, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setCursor(211, 202);
        tft.print("MENU");
        actionButton = 2;
        actionStartTime = now;
      }
    } else if (currentPage == 1) {
      if (x > 10 && x < 310 && y > 50 && y < 90) {
        tft.fillRect(10, 50, 300, 40, TFT_ORANGE);
        tft.setTextColor(TFT_BLACK, TFT_ORANGE);
        tft.setCursor(100, 62);
        tft.print("RESET WiFi");
        actionButton = 10;
        actionStartTime = now;
      } else if (x > 10 && x < 150 && y > 100 && y < 140) {
        tft.fillRect(10, 100, 140, 40, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setCursor(32, 112);
        tft.print("Set Time");
        actionButton = 11;
        actionStartTime = now;
      } else if (x > 10 && x < 150 && y > 150 && y < 190) {
        tft.fillRect(10, 150, 140, 40, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setCursor(26, 162);
        tft.print("Check Time");
        actionButton = 12;
        actionStartTime = now;
      } else if (x > 170 && x < 310 && y > 100 && y < 140) {
        tft.fillRect(170, 100, 140, 40, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setCursor(186, 112);
        tft.print("Set Limit");
        actionButton = 13;
        actionStartTime = now;
      } else if (x > 170 && x < 310 && y > 150 && y < 190) {
        tft.fillRect(170, 150, 140, 40, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setCursor(204, 162);
        tft.print("Set Up");
        actionButton = 14;
        actionStartTime = now;
      } else if (y > 200) {
        backButtonEffect();
        actionButton = 15;
        actionStartTime = now;
      }
    } else if (currentPage == 2) {
      if (x > 55 && x < 155 && y > 130 && y < 180) {
        tft.fillRect(55, 130, 100, 50, TFT_ORANGE);
        tft.setTextColor(TFT_BLACK, TFT_ORANGE);
        tft.setCursor(87, 147);
        tft.print("YES");
        actionButton = 20;
        actionStartTime = now;
      } else if (x > 165 && x < 265 && y > 130 && y < 180) {
        tft.fillRect(165, 130, 100, 50, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setCursor(203, 147);
        tft.print("NO");
        actionButton = 21;
        actionStartTime = now;
      }
    } else if (currentPage == 3) {
      if (x > 60 && x < 260 && y > 20 && y < 70) {
        tft.fillRect(60, 20, 200, 50, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextSize(3);
        tft.setCursor(152, 33);
        tft.print("+");
        actionButton = 30;
        actionStartTime = now;
      } else if (x > 60 && x < 260 && y > 180 && y < 230) {
        tft.fillRect(60, 180, 200, 50, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextSize(3);
        tft.setCursor(152, 193);
        tft.print("-");
        actionButton = 31;
        actionStartTime = now;
      } else if (x > 70 && x < 150 && y > 135 && y < 165) {
        tft.fillRect(70, 135, 80, 30, TFT_LIGHTGREEN);
        tft.setTextColor(TFT_BLACK, TFT_LIGHTGREEN);
        tft.setTextSize(2);
        tft.setCursor(92, 142);
        tft.print("YES");
        actionButton = 32;
        actionStartTime = now;
      } else if (x > 170 && x < 250 && y > 135 && y < 165) {
        tft.fillRect(170, 135, 80, 30, TFT_ORANGE);
        tft.setTextColor(TFT_BLACK, TFT_ORANGE);
        tft.setTextSize(2);
        tft.setCursor(198, 142);
        tft.print("NO");
        actionButton = 33;
        actionStartTime = now;
      }
    } else if (currentPage == 4) {
      if (y > 50 && y < 80) {
        int i = (x - 10) / 78;
        if (i >= 0 && i <= 3) {
          int xPos = 10 + (i * 78);
          if (x >= xPos && x <= xPos + 68) {
            tft.fillRect(xPos, 50, 68, 30, TFT_WHITE);
            tft.setTextColor(TFT_BLACK);
            tft.setCursor(xPos + 28, 57);
            tft.print("+");
            actionButton = 40 + i;
            actionStartTime = now;
          }
        }
      } else if (y > 160 && y < 190) {
        int i = (x - 10) / 78;
        if (i >= 0 && i <= 3) {
          int xPos = 10 + (i * 78);
          if (x >= xPos && x <= xPos + 68) {
            tft.fillRect(xPos, 160, 68, 30, TFT_WHITE);
            tft.setTextColor(TFT_BLACK);
            tft.setCursor(xPos + 28, 167);
            tft.print("-");
            actionButton = 44 + i;
            actionStartTime = now;
          }
        }
      } else if (y > 200) {
        if (x < 160) {
          tft.fillRect(0, 200, 160, 40, TFT_LIGHTGREEN);
          tft.setTextColor(TFT_BLACK, TFT_LIGHTGREEN);
          tft.setCursor(62, 212);
          tft.print("YES");
          actionButton = 48;
        } else {
          tft.fillRect(160, 200, 160, 40, TFT_ORANGE);
          tft.setTextColor(TFT_BLACK, TFT_ORANGE);
          tft.setCursor(228, 212);
          tft.print("NO");
          actionButton = 49;
        }
        actionStartTime = now;
      }
    } else if (currentPage == 5) {
      if (y > 200) {
        backButtonEffect();
        actionButton = 15;
        actionStartTime = now;
      } else if (x > 260 && y >= 80 && y < 200) {
        int row = (y - 80) / 40;
        if (row >= 0 && row <= 2) {
          int yPos = 80 + (row * 40);
          int yDEL = 87 + (row * 40);
          if (y >= yPos && y <= yPos + 30) {
            tft.fillRect(270, yPos, 40, 30, TFT_ORANGE);
            tft.setTextColor(TFT_BLACK, TFT_ORANGE);
            tft.setCursor(272, yDEL);
            tft.print("DEL");
            actionButton = 50 + row;
            actionStartTime = now;
          }
        }
      }
    } else if (currentPage == 6) {
      if (y > 200) {
        backButtonEffect();
        actionButton = 15;
        actionStartTime = now;
      } else if (x > 30 && x < 130 && y > 76 && y < 116) {
        tft.fillRect(30, 76, 100, 40, TFT_WHITE);
        tft.drawRect(30, 76, 100, 40, TFT_BLACK);
        tft.setTextColor(TFT_BLACK);
        tft.setCursor(74, 88);
        tft.print("+");
        actionButton = 60;
        actionStartTime = now;
      } else if (x > 30 && x < 130 && y > 150 && y < 190) {
        tft.fillRect(30, 150, 100, 40, TFT_WHITE);
        tft.drawRect(30, 150, 100, 40, TFT_BLACK);
        tft.setTextColor(TFT_BLACK);
        tft.setCursor(74, 162);
        tft.print("-");
        actionButton = 61;
        actionStartTime = now;
      } else if (x > 190 && x < 290 && y > 76 && y < 116) {
        tft.fillRect(190, 76, 100, 40, TFT_WHITE);
        tft.drawRect(190, 76, 100, 40, TFT_BLACK);
        tft.setTextColor(TFT_BLACK);
        tft.setCursor(234, 88);
        tft.print("+");
        actionButton = 62;
        actionStartTime = now;
      } else if (x > 190 && x < 290 && y > 150 && y < 190) {
        tft.fillRect(190, 150, 100, 40, TFT_WHITE);
        tft.drawRect(190, 150, 100, 40, TFT_BLACK);
        tft.setTextColor(TFT_BLACK);
        tft.setCursor(234, 162);
        tft.print("-");
        actionButton = 63;
        actionStartTime = now;
      }
    } else if (currentPage == 7) {
      if (y > 200) {
        backButtonEffect();
        actionButton = 15;
        actionStartTime = now;
      } else if (x > 60 && x < 260 && y > 140 && y < 190) {
        tft.fillRect(60, 140, 200, 50, TFT_WHITE);
        actionButton = 70;
        actionStartTime = now;
      }
    }
  }
}

void executePendingAction(unsigned long now) {
  if (actionButton == 0) return;
  unsigned long waitTime = resetDelay;

  // 💡 ปุ่ม + และ - ลดดีเลย์ให้สั้นลง (200ms) เพื่อให้กดรัวๆ ได้
  if (actionButton == 30 || actionButton == 31 || (actionButton >= 40 && actionButton <= 47) || (actionButton >= 60 && actionButton <= 63)) {
    waitTime = PLUS_MINUS;
  }

  if (now - actionStartTime >= waitTime) {
    int lastAction = actionButton;
    switch (actionButton) {
      case 1:
        drawSetTimeFeedPage();
        break;
      case 2:
        drawMenuPage();
        break;
      case 10:
        confirmMode = 1;
        drawConfirmPage("Confirm Reset", "WiFi?", 1);
        break;
      case 11:
        drawSetTime();
        break;
      case 12:
        drawCheckSetTime();
        break;
      case 13:
        drawSetLimitPage();
        break;
      case 14:
        drawSetUpPage();
        break;
      case 15:
        if (currentPage == 5 || currentPage == 6 || currentPage == 7) {
          saveSettings();
          drawMenuPage();
        } else {
          drawHomePage();
        }
        break;
      case 20:
        if (confirmMode == 1) {
          resetWiFi();
        } else if (confirmMode == 2) {
          if (WiFi.status() == WL_CONNECTED) {
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", schedules[deleteIdx].hour, schedules[deleteIdx].minute);
            String json = "{\"type\":\"delete_schedule_from_esp\", \"role\":\"main\", \"token\":\"" + String(myToken) + "\", \"time\":\"" + String(timeStr) + "\"}";
            client.send(json);
            DEBUG_PRINTLN("🗑️ Sent Delete Request to Server: " + json);
          }
          schedules[deleteIdx].hour = 0;
          schedules[deleteIdx].minute = 0;
          schedules[deleteIdx].gram = 10;
          schedules[deleteIdx].active = false;
          saveSettings();
          drawCheckSetTime();
        }
        break;
      case 21:
        if (confirmMode == 1) drawMenuPage();
        else if (confirmMode == 2) drawCheckSetTime();
        break;

      case 30:
        if (tempFeedAmount < limitBowlFood) {
          tempFeedAmount += 10;
          updateTimerFeedValue(tempFeedAmount);
        }
        drawButtonEffect(60, 20, "+");
        break;
      case 31:
        if (tempFeedAmount > 10) {
          tempFeedAmount -= 10;
          updateTimerFeedValue(tempFeedAmount);
        }
        drawButtonEffect(60, 180, "-");
        break;
      case 32:
        manualFeedAmount = tempFeedAmount;
        DEBUG_PRINTLN(manualFeedAmount);
        startFeeding(manualFeedAmount, ADD_MORE, now);
        drawHomePage();
        break;
      case 33:
        drawHomePage();
        break;

      case 40:
        schedules[editIdx].hour = (schedules[editIdx].hour + 1) % 24;
        break;
      case 41:
        schedules[editIdx].minute = (schedules[editIdx].minute + 1) % 60;
        break;
      case 42:
        editIdx = (editIdx + 1) % 3;
        break;
      case 43:
        if (schedules[editIdx].gram <= 390) {
          schedules[editIdx].gram += 10;
        }
        break;
      case 44:
        schedules[editIdx].hour = (schedules[editIdx].hour == 0) ? 23 : schedules[editIdx].hour - 1;
        break;
      case 45:
        schedules[editIdx].minute = (schedules[editIdx].minute == 0) ? 59 : schedules[editIdx].minute - 1;
        break;
      case 46:
        editIdx = (editIdx == 0) ? 2 : editIdx - 1;
        break;
      case 47:
        if (schedules[editIdx].gram > 10) {
          schedules[editIdx].gram -= 10;
        }
        break;
      case 48:
        schedules[editIdx].active = true;
        saveSettings();
        DEBUG_PRINTF("Saved Slot %d: %02d:%02d (%ds)\n", editIdx + 1, schedules[editIdx].hour, schedules[editIdx].minute, schedules[editIdx].gram);
        if (WiFi.status() == WL_CONNECTED) {
          char timeStr[6];
          sprintf(timeStr, "%02d:%02d", schedules[editIdx].hour, schedules[editIdx].minute);
          String json = "{\"type\":\"add_schedule_from_esp\", \"role\":\"main\", \"token\":\"" + String(myToken) + "\", \"time\":\"" + String(timeStr) + "\", \"duration\":" + String(schedules[editIdx].gram) + ", \"slot\":" + String(editIdx + 1) + "}";
          client.send(json);
          DEBUG_PRINTLN("📤 Synced Schedule to Server: " + json);
        }
        drawMenuPage();
        break;
      case 49:
        drawMenuPage();
        break;
      case 50:
      case 51:
      case 52:
        confirmMode = 2;
        deleteIdx = lastAction - 50;
        drawConfirmPage("Confirm Delete", "Round: " + String(deleteIdx + 1), 2);
        break;

      case 60:
        limitBowlFood += 100;
        updateLimitValues();
        restoreLimitButton(60);
        break;
      case 61:
        if (limitBowlFood > 100) limitBowlFood -= 100;
        updateLimitValues();
        restoreLimitButton(61);
        break;
      case 62:
        limitBowlWater += 100;
        updateLimitValues();
        restoreLimitButton(62);
        break;
      case 63:
        if (limitBowlWater > 100) limitBowlWater -= 100;
        updateLimitValues();
        restoreLimitButton(63);
        break;

      case 70:
        maintenanceMode = !maintenanceMode;
        if (maintenanceMode) {
          feedServo.detach();
          DEBUG_PRINTLN(">>> System LOCKED by User");
        } else {
          DEBUG_PRINTLN(">>> System UNLOCKED");
        }
        drawSetUpPage();
        break;
    }
    if (lastAction >= 40 && lastAction <= 47) {
      updateSetTimeValues();
    }
    setTimeEffectButton(lastAction);
    actionButton = 0;
  }
}

// ============================================================================
// ======================= 🎬 หมวดที่ 13: MAIN EXECUTION ========================
// ============================================================================
void setup() {
  Serial.begin(115200);

  // 🌟🌟🌟 THE MAGIC FIX V2 (Wake up Hardware) 🌟🌟🌟
  // 1. เคลียร์สาย I2C (เลเซอร์ VL53L0X) ให้หลุดจากอาการค้าง
  pinMode(21, INPUT_PULLUP);  // SDA ปล่อยเป็น Input ชั่วคราวไม่ให้ชนกับเซนเซอร์
  pinMode(22, OUTPUT);        // SCL ขานาฬิกา
  for (int i = 0; i < 10; i++) {
    digitalWrite(22, LOW);
    delayMicroseconds(10);
    digitalWrite(22, HIGH);
    delayMicroseconds(10);
  }
  Wire.begin(21, 22);

  // 2. ปลุกตาชั่ง (HX711) ให้ตื่นจากการ Sleep (ดึง SCK กลับมาเป็น LOW)
  pinMode(HX_BowlWater_SCK, OUTPUT);
  digitalWrite(HX_BowlWater_SCK, LOW);
  pinMode(HX_TankWater_SCK, OUTPUT);
  digitalWrite(HX_TankWater_SCK, LOW);
  pinMode(HX_BowlFood_SCK, OUTPUT);
  digitalWrite(HX_BowlFood_SCK, LOW);
  delay(10);  // รอให้ชิปตาชั่งเซ็ตอัปตัวเองแป๊บเดียว
  // 🌟🌟🌟 END MAGIC FIX V2 🌟🌟🌟

  rtcStart();  // เปิดนาฬิกา Hardware

  if (!wifiStatus && checkRtc) {
    syncSystemTimeFromRtc();
  }

  if (!EEPROM.begin(EEPROM_SIZE)) {
    DEBUG_PRINTLN("failed to initialise EEPROM");
  } else {
    loadSettings();
  }

  // เซ็ตอัพระบบ Relay ปั๊มน้ำ
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  // เทสขยับ Servo ตอนเริ่มเครื่อง
  feedServo.attach(SERVO_PIN);
  feedServo.write(SPEED_STOP);
  delay(300);
  feedServo.detach();

  CamSerial.begin(9600, SERIAL_8N1, 35, 2);

  // 🌟 เปิดหน้าจอขึ้นมาก่อน (กันจอดำตอนรอเชื่อมเน็ต)
  tft.init();
  tft.setRotation(1);
  ts.begin();

  // 💡 วาดหน้าจอบอกสถานะตอนเสียบปลั๊กกำลังต่อเน็ต
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(60, 100);
  tft.print("Connecting WiFi...");

  startWifi();

  // เตรียมระบบรอรับโค้ดลอยฟ้า (OTA)
  // เตรียมระบบรอรับโค้ดลอยฟ้า (OTA)
  ArduinoOTA.setHostname("Smart-Pet-ESP32-30PIN");
  ArduinoOTA.setPassword("1234");
  ArduinoOTA.onStart([]() {
    DEBUG_PRINTLN("\n>>> Start OTA Update... Stopping all hardware!");
    // ตัดไฟมอเตอร์และปั๊มน้ำ ป้องกันอุปกรณ์ทำงานค้างตอนอัปเดต
    digitalWrite(PUMP_PIN, LOW);
    feedServo.detach();
    client.close();  // ปิดการสตรีมมิ่ง WebSocket เพื่อคืน RAM
  });
  ArduinoOTA.begin();
  
  drawHomePage();

  // ตั้งค่าเซนเซอร์
  loadCellBowlFood();
  loadCellBowlWater();
  loadCellTankWater();
  vl53l0xFood();

  updateTankLevel();
  forceUpdateTank = true;

  client.onMessage(onMessageCallback);

  // 🛠️ โค้ดช่าง: เก็บไว้ใช้วัดค่า Tare Offset ถาดน้ำหนักเมื่อเปลี่ยนชามใหม่
  // DEBUG_PRINTLN("=== TARE OFFSETS ===");
  // DEBUG_PRINTLN(LoadCell_BowlFood.getTareOffset());
  // DEBUG_PRINTLN(LoadCell_BowlWater.getTareOffset());
  // DEBUG_PRINTLN(LoadCell_TankWater.getTareOffset());
}

void loop() {
  ArduinoOTA.handle();  // คอยเช็คว่ามีการกดอัปโหลด OTA เข้ามาไหม

  unsigned long currentTime = millis();  // 💡 ใช้เวลาตรงนี้เป็นฐานให้ทุกระบบทำงานโดยไม่อ้างอิง delay()

  // --- 1. ตรวจสอบการเชื่อมต่อ ---
  checkInternetConnection(currentTime);
  checkStatusWifi();
  handleCameraSync(currentTime);
  handleWebSocket(currentTime);

  // --- 2. รับคำสั่งจากหน้าจอทัชสกรีน ---
  executePendingAction(currentTime);
  checkTouch(currentTime);
  updateHomeDynamic(currentTime);  // 💡 อัปเดตเฉพาะตัวหนังสือบนจอเพื่อไม่ให้กระพริบ

  // --- 3. อ่านค่าเซนเซอร์แบบ Real-time ---
  loadCellBowlFoodWork(currentTime);
  vl53l0xFoodWork(currentTime);
  loadCellWaterBowlWork();
  loadCellWaterTankWork();

  // --- 4. สมองกลประมวลผลการทำงาน ---
  processSchedule(currentTime);
  processFeeder(currentTime);
  processWater(currentTime);

  // 🛠️ โค้ดช่าง: เก็บไว้เช็คเปอร์เซ็นต์ RAM ที่ว่างอยู่
  // checkESP32_RAM(currentTime);
}