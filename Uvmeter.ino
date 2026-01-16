#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>  // เพิ่ม Library เวลา
#include <WiFiUdp.h>    // เพิ่ม Library UDP สำหรับ NTP

// --- ตั้งค่า Firebase ---
#define FIREBASE_HOST "myuvmonitor-default-rtdb.asia-southeast1.firebasedatabase.app" 
#define FIREBASE_AUTH "XXXXX"

// --- ตั้งค่า WiFi ---
char ssid_home[] = "Prapakon_2.4G";
char pass_home[] = "XXXXX";
const char *ssid_ap = "UV_Monitor_Pro";
const char *pass_ap = "XXXXX";

// ตัวแปรระบบ
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600); // GMT+7 ไทย

bool isOfflineMode = false;
unsigned long lastHistorySend = 0; 
unsigned long lastSampleTick = 0;

// ตัวแปรสำหรับคำนวณค่าเฉลี่ยรายชั่วโมง
float uvSum = 0;
int sampleCount = 0;
int lastSavedHour = -1;

// ฟังก์ชันหน้าเว็บสำหรับโหมดพรีเซนต์
void handleRoot() {
  float uvIndex = readUV();
  String html = "<html><head><meta charset='UTF-8' http-equiv='refresh' content='2'>";
  html += "<style>body{background:#0f172a; color:white; font-family:sans-serif; text-align:center; padding-top:50px;}";
  html += "h1{font-size:100px; color:#4ade80; margin:10px 0;} p{font-size:24px; color:#94a3b8;}</style></head><body>";
  html += "<h2>Project: UV Monitor</h2>";
  html += "<p>สถานะ: โหมดพรีเซนต์ (Offline)</p>";
  html += "<h1>" + String(uvIndex, 1) + "</h1>";
  html += "<p>UV INDEX</p></body></html>";
  server.send(200, "text/html", html);
}

float readUV() {
  int rawADC = analogRead(A0);
  float voltage = rawADC * (3.3 / 1023.0);
  return voltage / 0.1;
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid_home, pass_home);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    timeClient.begin(); // เริ่มดึงเวลาจากเน็ต
    isOfflineMode = false;
  } else {
    WiFi.disconnect();
    WiFi.softAP(ssid_ap, pass_ap);
    server.on("/", handleRoot);
    server.begin();
    isOfflineMode = true;
  }
}

// --- ปรับปรุงส่วน Loop ในโค้ด MCU ของคุณ ---

void loop() {
  float uvIndex = readUV();

  if (!isOfflineMode) {
    timeClient.update();
    int currentHour = timeClient.getHours();

    // 1. ส่ง Real-time (ทุก 3 วินาที) - ตัวเลขหน้าเว็บจะเปลี่ยน
    static unsigned long lastRT = 0;
    if (millis() - lastRT > 3000) {
      Firebase.setFloat(fbdo, "/UV_Sensor/current", uvIndex);
      lastRT = millis();
    }

    // 2. ส่ง History (ปรับเป็น 10 วินาที) - กราฟเส้นจะขยับ
    static unsigned long lastHistory = 0;
    if (millis() - lastHistory > 10000) { // <--- เปลี่ยนจาก 60000 เป็น 10000
      FirebaseJson json;
      json.set("uv", uvIndex);
      Firebase.pushJSON(fbdo, "/UV_Sensor/history", json);
      lastHistory = millis();
    }

    // 3. ระบบคำนวณค่าเฉลี่ยรายชั่วโมง (ยังคงความแม่นยำทุก 1 นาที)
    if (millis() - lastSampleTick > 60000) {
      uvSum += uvIndex;
      sampleCount++;
      lastSampleTick = millis();
    }

    if (currentHour != lastSavedHour && sampleCount > 0) {
      float hourlyAvg = uvSum / sampleCount;
      FirebaseJson hourJson;
      hourJson.set("avg", hourlyAvg);
      hourJson.set("hour", currentHour);
      if (Firebase.pushJSON(fbdo, "/UV_Sensor/hourly_averages", hourJson)) {
        uvSum = 0; sampleCount = 0;
        lastSavedHour = currentHour;
      }
    }
  } else {
    server.handleClient();
  }
}