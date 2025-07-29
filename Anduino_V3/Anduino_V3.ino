/*
                               居家運動助手 - 電腦輔助捲曲運動 (M5Stack Core2 版本)

     -----------------------------------代碼操作指南----------------------------------
    |   1.  [第 19-20 行]  修改 WiFi 名稱和密碼                                      |
    |   2.  [第 23 行]     修改 Jetson nano IP 地址                                  |
    -----------------------------------------------------------------------------------
  
*/

#include <M5Core2.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ===== 配置常數 =====
// WiFi 配置
const char* WIFI_SSID = "**********";  // WiFi 名稱
const char* WIFI_PASSWORD = "********";      // WiFi 密碼

// Flask 伺服器配置
const char* SERVER_HOST = "192.168.1.111";  // IP 地址 - 請確認這是Python伺服器的實際IP
const uint16_t SERVER_PORT = 5000;          // Flask 預設通訊埠
const char* COUNT_ENDPOINT = "/count";       // 計數端點

// 連接時間常數
const unsigned long WIFI_TIMEOUT = 20000;      // WiFi 連接超時（毫秒）
const unsigned long SERVER_CHECK_INTERVAL = 5000;  // 伺服器連接檢查間隔（毫秒）
const unsigned long HTTP_TIMEOUT = 3000;       // HTTP 請求超時（毫秒）
const unsigned long MIN_SEND_INTERVAL = 2000;  // 數據發送最小間隔（毫秒）
const unsigned long CONNECTION_RETRY_INTERVAL = 10000; // 連接重試間隔

// 運動檢測常數
const float GYRO_X_CURL_THRESHOLD = 80.0f;
const float GYRO_Z_CURL_THRESHOLD = -10.0f;
const float GYRO_X_RELEASE_THRESHOLD = 60.0f;
const float GYRO_Z_RELEASE_THRESHOLD = 5.0f;
const float ACC_Y_RELEASE_THRESHOLD = 0.5f;
const float ARM_ANGLE_CHANGE_RATE = 5.0f;      // 手臂角度變化率（每幀度數）
const float PITCH_CURL_THRESHOLD = -30.0f;    // 彎曲時的 pitch 閾值（負值表示向下彎曲）
const float PITCH_RELEASE_THRESHOLD = -10.0f;  // 釋放時的 pitch 閾值

// 運動檢測常數
const float ROLL_CURL_THRESHOLD = 70.0f;     // Roll > 70 度時視為彎曲
const float ROLL_RELEASE_THRESHOLD = 10.0f;  // Roll < 10 度時視為釋放

// ===== 全局常數與變數 =====
// 顏色定義
#define TXT_COLOR GREEN
#define BG_COLOR BLACK
#define WARN_COLOR YELLOW
#define ERROR_COLOR RED
#define SUCCESS_COLOR GREEN

// 顯示參數
#define STATUS_Y 220  // 狀態文字的 Y 座標
#define ARM_CENTER_X 270
#define ARM_CENTER_Y 180
#define ARM_LENGTH 30
#define FOREARM_LENGTH 60

// IMU 數據
float accX = 0.0f, accY = 0.0f, accZ = 0.0f;
float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
float temp = 0.0f;

// 姿態數據
float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;

// 虛擬手臂角度
float armAngle = 180.0f;                // 初始伸直手臂位置（180度）
float targetArmAngle = 180.0f;          // 目標手臂角度，用於平滑過渡

// ===== 全局變數 =====
// 連接狀態
Preferences preferences;                // 用於保存持久數據
HTTPClient http;                        // HTTP 客戶端
unsigned long lastServerCheckTime = 0;  // 上次伺服器連接檢查時間
unsigned long lastConnectionRetryTime = 0; // 上次連接重試時間
bool serverConnected = false;           // 伺服器連接狀態
int connectionAttempts = 0;             // 連接嘗試次數

// 運動狀態
int motionState = 0;                    // 0: 閒置/釋放, 1: 捲曲
int previousMotionState = 0;            // 先前運動狀態
unsigned long lastSendTime = 0;         // 上次數據發送時間
int curlCount = 0;                      // 完成捲曲次數
unsigned long lastMotionTime = 0;       // 上次運動檢測時間
unsigned long lastStateChangeTime = 0;  // 上次狀態變化時間
const unsigned long STATE_CHANGE_DEBOUNCE = 500; // 500毫秒防抖時間

// ===== 函數原型 =====
bool connectToWiFi(unsigned long timeout);
void updateIMUData();
void detectMotion();
void drawVirtualArm();
void displayInfo();
void updateStatus(const char* message, uint16_t color = TXT_COLOR);
bool sendCountToServer(int count);
void resetCurlCount();
void handleButtons();
void drawUI();
float degToRad(float degrees);
void incrementCurlCount();
float mapValue(float value, float fromLow, float fromHigh, float toLow, float toHigh);

// ===== 全局變數 =====
int i = 0; // 用於循環計數

// ===== 設置函數 =====
void setup() {

  // 初始化 M5Stack
  M5.begin();
  M5.IMU.Init();
  
  // 初始化偏好設置
  preferences.begin("exercise", false);
  curlCount = preferences.getInt("curlCount", 0);
  preferences.end();
  
  // 設置顯示
  M5.Lcd.fillScreen(BG_COLOR);
  M5.Lcd.setTextColor(TXT_COLOR, BG_COLOR);
  M5.Lcd.setTextSize(2);
  
  // 繪製初始界面
  drawUI();
  
  // 連接到 WiFi
  updateStatus("WiFi Connecting...");
  if (connectToWiFi(WIFI_TIMEOUT)) {
    updateStatus("WiFi Connected", TXT_COLOR);
    
    // 顯示網路資訊
    Serial.println("=== Network Information ===");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("Target Server: ");
    Serial.print(SERVER_HOST);
    Serial.print(":");
    Serial.println(SERVER_PORT);
    Serial.println("===========================");
    
    // 在螢幕上顯示IP資訊
    M5.Lcd.fillRect(0, 220, 320, 20, BG_COLOR);
    M5.Lcd.setCursor(0, 220);
    M5.Lcd.printf("IP: %s", WiFi.localIP().toString().c_str());
    
    updateStatus("WiFi Connected", SUCCESS_COLOR);
  } else {
    updateStatus("WiFi Connection Failed", ERROR_COLOR);
  }
}

// ===== 主循環 =====
void loop() {
  // 更新按鈕狀態
  M5.update();
  handleButtons();
  
  unsigned long currentTime = millis();
  
  // 定期檢查 WiFi 和伺服器連接
  if (currentTime - lastServerCheckTime > SERVER_CHECK_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected");
      updateStatus("WiFi Disconnected", ERROR_COLOR);
      serverConnected = false;
      
      // 如果距離上次嘗試已有足夠時間，則重試連接
      if (currentTime - lastConnectionRetryTime > CONNECTION_RETRY_INTERVAL) {
        Serial.println("Attempting to reconnect WiFi...");
        connectToWiFi(WIFI_TIMEOUT);
        lastConnectionRetryTime = currentTime;
        connectionAttempts++;
      }
    } else {
      // WiFi已連接，暫停伺服器檢查
      if (!serverConnected) {
        serverConnected = true;
        updateStatus("WiFi Connected", SUCCESS_COLOR);
      }
    }
    
    lastServerCheckTime = currentTime;
  }
  
  // 讀取並處理 IMU 數據
  updateIMUData();
  
  // 顯示更新的資訊
  displayInfo();
  
  // 檢查捲曲動作
  if (currentTime - lastMotionTime > 50) {  // 限制檢測頻率
    detectMotion();
    lastMotionTime = currentTime;
  }
  
  // 平滑過渡手臂角度
  if (armAngle != targetArmAngle) {
    if (armAngle < targetArmAngle) {
      armAngle = min(armAngle + ARM_ANGLE_CHANGE_RATE, targetArmAngle);
    } else {
      armAngle = max(armAngle - ARM_ANGLE_CHANGE_RATE, targetArmAngle);
    }
  }
  
  // 根據當前角度繪製虛擬手臂
  drawVirtualArm();
  
  // 保存先前運動狀態
  previousMotionState = motionState;

  // 更新狀態顯示
  static int lastSentCurlCount = -1;
  if (curlCount != lastSentCurlCount && serverConnected) {
    if (sendCountToServer(curlCount)) {
      Serial.println("Successfully sent count to server");
      lastSentCurlCount = curlCount;
    }
  }

  // 短延遲以防止循環太快
  delay(10);
}

// ===== WiFi 連接 =====
bool connectToWiFi(unsigned long timeout) {
  // 啟動 WiFi 連接
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // 設置連接超時
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
    M5.Lcd.print(".");
  }
  
  return (WiFi.status() == WL_CONNECTED);
}

// ===== 增加捲曲計數 =====
void incrementCurlCount() {
  curlCount++;
  
  // 將捲曲計數保存到偏好設置
  preferences.begin("exercise", false);
  preferences.putInt("curlCount", curlCount);
  preferences.end();
}

// ===== 更新 IMU 數據 =====
void updateIMUData() {
  // 讀取 IMU 數據
  M5.IMU.getGyroData(&gyroX, &gyroY, &gyroZ);
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  M5.IMU.getAhrsData(&pitch, &roll, &yaw);
  M5.IMU.getTempData(&temp);
  
  // 實時更新手臂角度以模擬當前Roll角度
  // 將Roll角度範圍映射到手臂角度範圍
  // Roll 0~90度 映射到 手臂角度 180~90度
  float mappedAngle = mapValue(roll, 0, 90, 180, 90);
  
  // 限制在合理範圍內
  if (mappedAngle < 90) mappedAngle = 90;
  if (mappedAngle > 180) mappedAngle = 180;
  
  // 只有角度變化超過 2 度才更新目標角度
  if (abs(targetArmAngle - mappedAngle) > 2.0f) {
    targetArmAngle = mappedAngle;
  }
}

// ===== 運動檢測 =====
void detectMotion() {
  unsigned long currentTime = millis();
  
  // 如果距離上次狀態變化時間太短，則跳過
  if (currentTime - lastStateChangeTime < STATE_CHANGE_DEBOUNCE) {
    return;
  }
  
  // 記錄先前狀態
  int prevState = motionState;
  
  // 使用 roll (AHRS X軸角度) 來檢測捲曲動作
  
  // 檢測捲曲動作：當 roll 角度大於閾值時
  if (roll > ROLL_CURL_THRESHOLD && motionState == 0) {
    // 判斷為捲曲動作
    motionState = 1;  // 設置狀態為捲曲
    lastStateChangeTime = currentTime;
  }
  // 檢測釋放動作：當 roll 角度小於閾值且當前為捲曲狀態
  else if (roll < ROLL_RELEASE_THRESHOLD && motionState == 1) {
    // 判斷為釋放動作
    motionState = 0;  // 重置狀態為非捲曲
    
    // 當從捲曲狀態轉為釋放狀態時，增加計數
    if (prevState == 1) {
      incrementCurlCount();
    }
    
    lastStateChangeTime = currentTime;
  }
}

// ===== 繪製虛擬手臂 =====
void drawVirtualArm() {
  // 清除先前手臂區域
  M5.Lcd.fillRect(ARM_CENTER_X - FOREARM_LENGTH - 10, 
                 ARM_CENTER_Y - FOREARM_LENGTH - 10, 
                 2 * FOREARM_LENGTH + 20, 
                 2 * FOREARM_LENGTH + 20, BG_COLOR);

  // 上臂固定，從中心垂直向下繪製
  int shoulderX = ARM_CENTER_X;
  int shoulderY = ARM_CENTER_Y;
  int elbowX = shoulderX;
  int elbowY = shoulderY + ARM_LENGTH;
  
  // 繪製上臂（二頭肌）
  M5.Lcd.drawLine(shoulderX, shoulderY, elbowX, elbowY, GREEN);

  // 根據角度計算前臂位置
  float radians = degToRad(armAngle);
  int handX = elbowX + FOREARM_LENGTH * cos(radians);
  int handY = elbowY - FOREARM_LENGTH * sin(radians);

  // 繪製前臂
  M5.Lcd.drawLine(elbowX, elbowY, handX, handY, RED);
  
  // 繪製手部為小圓圈
  M5.Lcd.fillCircle(handX, handY, 5, BLUE);
  
  // 繪製肘部為小圓圈
  M5.Lcd.fillCircle(elbowX, elbowY, 3, YELLOW);
}

// 將角度轉換為弧度
float degToRad(float degrees) {
  return degrees * PI / 180.0;
}

// ===== 顯示資訊 =====
void displayInfo() {
  // 清除先前數據區域
  M5.Lcd.fillRect(0, 60, 320, 100, BG_COLOR);
  
  // 捲曲計數顯示
  M5.Lcd.setCursor(0, 60);
  M5.Lcd.printf("Count: %d", curlCount);
  
  // 運動狀態顯示
  M5.Lcd.setCursor(0, 80);
  M5.Lcd.printf("Curl: %s", motionState ? "YES" : "NO");
  
  // 網路狀態顯示
  M5.Lcd.setCursor(0, 100);
  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.setTextColor(SUCCESS_COLOR, BG_COLOR);
    M5.Lcd.printf("WiFi: OK");
  } else {
    M5.Lcd.setTextColor(ERROR_COLOR, BG_COLOR);
    M5.Lcd.printf("WiFi: NO");
  }
  M5.Lcd.setTextColor(TXT_COLOR, BG_COLOR);
  
  // 伺服器連接狀態
  M5.Lcd.setCursor(160, 60);
  if (serverConnected) {
    M5.Lcd.setTextColor(SUCCESS_COLOR, BG_COLOR);
    M5.Lcd.printf("Server: OK");
  } else {
    M5.Lcd.setTextColor(ERROR_COLOR, BG_COLOR);
    M5.Lcd.printf("Server: NO");
  }
  M5.Lcd.setTextColor(TXT_COLOR, BG_COLOR);
}

// ===== 更新狀態顯示 =====
void updateStatus(const char* message, uint16_t color) {
  M5.Lcd.fillRect(0, STATUS_Y, 320, 20, BG_COLOR);
  M5.Lcd.setCursor(0, STATUS_Y);
  M5.Lcd.setTextColor(color, BG_COLOR);
  M5.Lcd.print(message);
  M5.Lcd.setTextColor(TXT_COLOR, BG_COLOR);  // 重置文字顏色
}

// ===== 發送計數到伺服器 =====
bool sendCountToServer(int countValue) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send count");
    return false;
  }
  
  String serverUrl = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + COUNT_ENDPOINT;
  
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);

  String payload = "{\"count\": " + String(countValue) + "}";
  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    Serial.println("Response: " + response);
    http.end();
    return true;
  } else {
    Serial.printf("HTTP POST failed, error code: %d\n", httpResponseCode);
    http.end();
    return false;
  }
}

// ===== 重置捲曲計數 =====
void resetCurlCount() {
  curlCount = 0;
  
  // 保存到偏好設置
  preferences.begin("exercise", false);
  preferences.putInt("curlCount", curlCount);
  preferences.end();
  
  // 更新顯示
  M5.Lcd.fillRect(160, 80, 160, 20, BG_COLOR);
  M5.Lcd.setCursor(160, 80);
  M5.Lcd.printf("Count: %d", curlCount);
  
  updateStatus("Curl count reset", SUCCESS_COLOR);
}

// ===== 處理按鈕按下 =====
void handleButtons() {
  // 檢查觸控位置來判斷按鈕點擊
  if (M5.Touch.ispressed()) {
    TouchPoint_t pos = M5.Touch.getPressPoint();
    
    // 重置按鈕 (0, 240, 160, 40)
    if (pos.x >= 0 && pos.x < 160 && pos.y >= 240 && pos.y < 280) {
      resetCurlCount();
      delay(300); // 防止連續觸發
    } 
    // 退出按鈕 (160, 240, 160, 40)
    else if (pos.x >= 160 && pos.x < 320 && pos.y >= 240 && pos.y < 280) {
      // 在實際應用中，可能實現省電模式或退出到選單
      updateStatus("Deep sleep mode", WARN_COLOR);
      delay(1000); // 顯示訊息
      ESP.deepSleep(0); // 進入深度睡眠模式
    }
  }
}

// ===== 繪製 UI 元素 =====
void drawUI() {
  // 標題
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.println("Home Exercise Helper");
  M5.Lcd.setCursor(0, 30);
  M5.Lcd.println("Computer-Assisted Curl");
  
  // 分隔線
  M5.Lcd.drawLine(0, 50, 320, 50, TXT_COLOR);

}

// 將值從一個範圍映射到另一個範圍
float mapValue(float value, float fromLow, float fromHigh, float toLow, float toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}
