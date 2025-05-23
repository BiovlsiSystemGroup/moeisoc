/*
                               居家運動助手 - 電腦輔助捲曲運動 (M5Stack Core2 版本)

    ------------------------------------LED 調試--------------------------------------
    |  WIFI -|  斷開連接  --> 顯示 "WiFi 連接中..."                                   |
    |        |  已連接    --> |  Jetson nano 伺服器已連接  --> 顯示 "已連接"           |
    |                         |  Jetson nano 伺服器斷開連接 --> 顯示 "錯誤"            |
    -----------------------------------------------------------------------------------

    ------------------------------------版本歷史--------------------------------------
    |   版本        日期         作者                      描述                       |
    |    0      2023.07.20    Jack Chen       原始版本。                              |
    |    1      2023.07.24    Jack Chen       修復延遲問題。                          |
    |    2      2023.08.08    Jack Chen       增加調試功能。                          |
    |    3      2025.05.20    Claude          M5Stack Core2 移植。                   |
    |    4      2025.05.20    Claude          英文UI + 虛擬手臂顯示。                |
    |    5      2025.05.21    Claude          優化代碼和邏輯。                       |
    |    6      2025.05.22    Claude          改進錯誤處理與連接恢復。               |
    |    7      2025.05.22    Claude          修復計數問題並解耦服務器依賴。         |
    -----------------------------------------------------------------------------------

     -----------------------------------代碼操作指南----------------------------------
    |   1.  [第 48-49 行]  修改 WiFi 名稱和密碼                                      |
    |   2.  [第 52 行]     修改 Jetson nano IP 地址                                  |
    -----------------------------------------------------------------------------------
  
*/

#include <M5Core2.h>
#include <WiFi.h>
#include <Preferences.h>

// ===== 配置常數 =====
// WiFi 配置
// const char* WIFI_SSID = "TP-Link_48CC";  // WiFi 名稱
// const char* WIFI_PASSWORD = "0912894730";      // WiFi 密碼
const char* WIFI_SSID = "Biovlsi_1011";  // WiFi 名稱
const char* WIFI_PASSWORD = "1011101110";      // WiFi 密碼

// Jetson Nano 伺服器配置
const char* SERVER_HOST = "192.168.50.254";  // IP 地址
const uint16_t SERVER_PORT = 8080;          // 通訊埠

// 連接時間常數
const unsigned long WIFI_TIMEOUT = 20000;      // WiFi 連接超時（毫秒）
const unsigned long SERVER_CHECK_INTERVAL = 5000;  // 伺服器連接檢查間隔（毫秒）
const unsigned long SERVER_RESPONSE_TIMEOUT = 3000; // 伺服器回應超時（毫秒）
const unsigned long MIN_SEND_INTERVAL = 500;   // 數據發送最小間隔（毫秒）
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

// 修改運動檢測常數為可調整的變數
float ROLL_CURL_THRESHOLD = 70.0f;     // Roll > 70 度時視為彎曲
float ROLL_RELEASE_THRESHOLD = 10.0f;  // Roll < 10 度時視為釋放

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
float prevAccZ = 0.0f;                  // 先前 Z 加速度，用於計算變化量
float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
float prevGyroZ = 0.0f;                 // 先前 Z 陀螺儀值，用於計算變化量
float temp = 0.0f;

// 姿態數據
float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;

// 虛擬手臂角度
float armAngle = 180.0f;                // 初始伸直手臂位置（180度）
float targetArmAngle = 180.0f;          // 目標手臂角度，用於平滑過渡

// ===== 全局變數 =====
// 連接狀態
Preferences preferences;                // 用於保存持久數據
WiFiClient client;                      // WiFi 客戶端
unsigned long lastServerCheckTime = 0;  // 上次伺服器連接檢查時間
unsigned long lastConnectionRetryTime = 0; // 上次連接重試時間
bool serverConnected = false;           // 伺服器連接狀態
int connectionAttempts = 0;             // 連接嘗試次數

// 運動狀態
int motionState = 0;                    // 0: 閒置/釋放, 1: 捲曲
int previousMotionState = 0;            // 先前運動狀態
bool awaitResponse = false;             // 是否等待伺服器回應
unsigned long lastSendTime = 0;         // 上次數據發送時間
int curlCount = 0;                      // 完成捲曲次數
unsigned long lastMotionTime = 0;       // 上次運動檢測時間
bool calibrationMode = false;           // 校準模式標誌
unsigned long lastStateChangeTime = 0;  // 上次狀態變化時間
const unsigned long STATE_CHANGE_DEBOUNCE = 500; // 500毫秒防抖時間
bool countFeedbackVisible = false;      // 是否顯示計數反饋
unsigned long countFeedbackTime = 0;    // 計數反饋顯示時間
const unsigned long COUNT_FEEDBACK_DURATION = 800; // 計數反饋顯示持續時間（毫秒）

// ===== 函數原型 =====
bool connectToWiFi(unsigned long timeout);
bool connectToServer();
void updateIMUData();
void detectMotion();
void drawVirtualArm();
void displaySensorData();
void updateStatus(const char* message, uint16_t color = TXT_COLOR);
void sendToServer(const String& data);
void checkServerResponse();
void resetCurlCount();
void handleButtons();
void drawUI();
float degToRad(float degrees);
void calibrateIMU();
float mapValue(float value, float fromLow, float fromHigh, float toLow, float toHigh);
void incrementCurlCount();
void showCountFeedback();
void hideCountFeedback();
void handleThresholdAdjustment();

// ===== 設置函數 =====
void setup() {
  // 初始化 M5Stack
  M5.begin();
  M5.IMU.Init();
  
  // 初始化偏好設置
  preferences.begin("exercise", false);
  curlCount = preferences.getInt("curlCount", 0);
  
  // 讀取閾值，若存在
  ROLL_CURL_THRESHOLD = preferences.getFloat("curlThreshold", 70.0f);
  ROLL_RELEASE_THRESHOLD = preferences.getFloat("releaseThreshold", 10.0f);
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
    
    // 嘗試連接到伺服器
    if (connectToServer()) {
      serverConnected = true;
      updateStatus("Server Connected", TXT_COLOR);
    } else {
      updateStatus("Server Connection Failed", WARN_COLOR);
    }
  } else {
    updateStatus("WiFi Connection Failed", ERROR_COLOR);
  }
  
  // 初始校準
  calibrateIMU();
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
      updateStatus("WiFi Disconnected", ERROR_COLOR);
      
      // 如果距離上次嘗試已有足夠時間，則重試連接
      if (currentTime - lastConnectionRetryTime > CONNECTION_RETRY_INTERVAL) {
        connectToWiFi(WIFI_TIMEOUT);
        lastConnectionRetryTime = currentTime;
        connectionAttempts++;
      }
    } else if (!client.connected()) {
      serverConnected = false;
      updateStatus("Server Disconnected", WARN_COLOR);
      
      // 如果距離上次嘗試已有足夠時間，則重試連接
      if (currentTime - lastConnectionRetryTime > CONNECTION_RETRY_INTERVAL) {
        if (connectToServer()) {
          serverConnected = true;
          updateStatus("Server Connected", TXT_COLOR);
        }
        lastConnectionRetryTime = currentTime;
        connectionAttempts++;
      }
    } else {
      // 連接良好，重置嘗試計數
      connectionAttempts = 0;
    }
    
    lastServerCheckTime = currentTime;
  }
  
  // 讀取並處理 IMU 數據
  updateIMUData();
  
  // 顯示更新的傳感器數據
  displaySensorData();
  
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
  
  // 檢查伺服器回應
  checkServerResponse();
  
  // 處理計數反饋顯示超時
  if (countFeedbackVisible && currentTime - countFeedbackTime > COUNT_FEEDBACK_DURATION) {
    hideCountFeedback();
  }
  
  // 保存先前運動狀態
  previousMotionState = motionState;
  
  // 只有當運動狀態變化時才向伺服器發送數據（如果連接）
  if (motionState != previousMotionState && 
      !awaitResponse && 
      currentTime - lastSendTime > MIN_SEND_INTERVAL &&
      serverConnected) {
    // 狀態變化，非等待回應狀態，且最小間隔已過
    if (motionState == 1) {
      sendToServer("1");  // 捲曲動作 (統一用數字1表示)
    } else {
      sendToServer("0");  // 釋放動作 (統一用數字0表示)
      // 注意：計數現在在 detectMotion() 中處理，不再依賴伺服器通信
    }
    awaitResponse = true;  // 設置等待回應標誌
    lastSendTime = currentTime;
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

// ===== 伺服器連接 =====
bool connectToServer() {
  // 如果 WiFi 未連接則提前返回
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  // 嘗試連接並設置超時
  client.setTimeout(3000);  // 設置套接字超時
  return client.connect(SERVER_HOST, SERVER_PORT);
}

// ===== 增加捲曲計數並顯示反饋 =====
void incrementCurlCount() {
  curlCount++;
  
  // 將捲曲計數保存到偏好設置
  preferences.begin("exercise", false);
  preferences.putInt("curlCount", curlCount);
  preferences.end();
  
  // 更新計數顯示
  M5.Lcd.fillRect(160, 80, 160, 20, BG_COLOR);
  M5.Lcd.setCursor(160, 80);
  M5.Lcd.printf("Count: %d", curlCount);
  
  // 顯示計數反饋
  showCountFeedback();
}

// ===== 顯示計數反饋 =====
void showCountFeedback() {
  // 顯示顯眼的計數反饋
  M5.Lcd.fillRect(100, 120, 120, 40, SUCCESS_COLOR);
  M5.Lcd.setCursor(105, 130);
  M5.Lcd.setTextColor(BG_COLOR, SUCCESS_COLOR);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf(" REP +1! ");
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TXT_COLOR, BG_COLOR);
  
  // 可選：播放蜂鳴聲或振動反饋
  // M5.Speaker.beep();
  
  countFeedbackVisible = true;
  countFeedbackTime = millis();
}

// ===== 隱藏計數反饋 =====
void hideCountFeedback() {
  M5.Lcd.fillRect(100, 120, 120, 40, BG_COLOR);
  countFeedbackVisible = false;
}

// ===== 更新 IMU 數據 =====
void updateIMUData() {
  // 保存先前值
  prevAccZ = accZ;
  prevGyroZ = gyroZ;
  
  // 讀取 IMU 數據
  M5.IMU.getGyroData(&gyroX, &gyroY, &gyroZ);
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  M5.IMU.getAhrsData(&pitch, &roll, &yaw);
  M5.IMU.getTempData(&temp);
  
  // 如果在校準模式，不更新手臂角度
  if (!calibrationMode) {
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
}

// ===== 校準 IMU =====
void calibrateIMU() {
  // 簡單校準 - 將當前姿態作為基準
  updateIMUData();
  
  // 在校準模式下停留一小段時間
  calibrationMode = true;
  updateStatus("Calibrating...", WARN_COLOR);
  
  float baseline_roll = 0;
  
  // 多次讀取以獲得平均基準值
  for (int i = 0; i < 10; i++) {
    updateIMUData();
    baseline_roll += roll;
    delay(50);
  }
  
  // 計算基準值平均
  baseline_roll /= 10;
  
  // 根據基準值調整閾值
  // 這將使得閾值相對於用戶的起始姿態而設定
  ROLL_CURL_THRESHOLD = baseline_roll + 60.0f;    // 基準值增加60度視為彎曲
  ROLL_RELEASE_THRESHOLD = baseline_roll + 15.0f; // 基準值增加15度視為釋放
  
  // 保存閾值到偏好設置
  preferences.begin("exercise", false);
  preferences.putFloat("curlThreshold", ROLL_CURL_THRESHOLD);
  preferences.putFloat("releaseThreshold", ROLL_RELEASE_THRESHOLD);
  preferences.end();
  
  armAngle = 180.0f;
  targetArmAngle = 180.0f;
  
  calibrationMode = false;
  updateStatus("Calibrated", TXT_COLOR);
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
    
    // 更新狀態顯示
    M5.Lcd.fillRect(0, 160, 320, 20, BG_COLOR);
    M5.Lcd.setCursor(0, 160);
    M5.Lcd.print("Motion: CURL");
    
    lastStateChangeTime = currentTime;
  }
  // 檢測釋放動作：當 roll 角度小於閾值且當前為捲曲狀態
  else if (roll < ROLL_RELEASE_THRESHOLD && motionState == 1) {
    // 判斷為釋放動作
    motionState = 0;  // 重置狀態為非捲曲
    
    // 更新狀態顯示
    M5.Lcd.fillRect(0, 160, 320, 20, BG_COLOR);
    M5.Lcd.setCursor(0, 160);
    M5.Lcd.print("Motion: RELEASE");
    
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

// ===== 調整閾值 =====
void handleThresholdAdjustment() {
  // 為閾值創建調整區域：螢幕左右邊緣
  
  // 顯示當前閾值
  M5.Lcd.fillRect(0, 140, 320, 20, BG_COLOR);
  M5.Lcd.setCursor(0, 120);
  M5.Lcd.printf("THC:%.1f", ROLL_CURL_THRESHOLD);
  M5.Lcd.setCursor(0, 140);
  M5.Lcd.printf("THR:%.1f", ROLL_RELEASE_THRESHOLD);
  bool thresholdChanged = false;
  
  // 檢查觸摸位置
  if (M5.Touch.ispressed()) {
    TouchPoint_t pos = M5.Touch.getPressPoint();
    
    // 左側邊緣：調整彎曲閾值
    if (pos.x >= 0 && pos.x < 40 && pos.y >= 50 && pos.y < 200) {
      // 上半部分：增加閾值
      if (pos.y < 125) {
        ROLL_CURL_THRESHOLD += 2.0f;
        if (ROLL_CURL_THRESHOLD > 120.0f) ROLL_CURL_THRESHOLD = 120.0f;
        updateStatus("Curl threshold +", WARN_COLOR);
      } 
      // 下半部分：減少閾值
      else {
        ROLL_CURL_THRESHOLD -= 2.0f;
        if (ROLL_CURL_THRESHOLD < ROLL_RELEASE_THRESHOLD + 10.0f) 
          ROLL_CURL_THRESHOLD = ROLL_RELEASE_THRESHOLD + 10.0f;
        updateStatus("Curl threshold -", WARN_COLOR);
      }
      thresholdChanged = true;
      delay(300); // 防止過快調整
    }
    
    // 右側邊緣：調整釋放閾值
    else if (pos.x >= 280 && pos.x < 320 && pos.y >= 50 && pos.y < 200) {
      // 上半部分：增加閾值
      if (pos.y < 125) {
        ROLL_RELEASE_THRESHOLD += 2.0f;
        if (ROLL_RELEASE_THRESHOLD > ROLL_CURL_THRESHOLD - 10.0f) 
          ROLL_RELEASE_THRESHOLD = ROLL_CURL_THRESHOLD - 10.0f;
        updateStatus("Release threshold +", WARN_COLOR);
      } 
      // 下半部分：減少閾值
      else {
        ROLL_RELEASE_THRESHOLD -= 2.0f;
        if (ROLL_RELEASE_THRESHOLD < 0.0f) ROLL_RELEASE_THRESHOLD = 0.0f;
        updateStatus("Release threshold -", WARN_COLOR);
      }
      thresholdChanged = true;
      delay(300); // 防止過快調整
    }
    
    // 儲存變更的閾值
    if (thresholdChanged) {
      preferences.begin("exercise", false);
      preferences.putFloat("curlThreshold", ROLL_CURL_THRESHOLD);
      preferences.putFloat("releaseThreshold", ROLL_RELEASE_THRESHOLD);
      preferences.end();
      
      // 更新閾值顯示
      M5.Lcd.fillRect(0, 140, 320, 20, BG_COLOR);
      M5.Lcd.setCursor(0, 120);
      M5.Lcd.printf("THC:%.1f", ROLL_CURL_THRESHOLD);
      M5.Lcd.setCursor(0, 140);
      M5.Lcd.printf("THR:%.1f", ROLL_RELEASE_THRESHOLD);
    }
  }
}

// ===== 顯示傳感器數據 =====
void displaySensorData() {
  // 清除先前數據區域
  M5.Lcd.fillRect(0, 60, 320, 80, BG_COLOR);
  
  // 顯示 AHRS 數據
  M5.Lcd.setCursor(0, 60);
  M5.Lcd.printf("Roll: %.1f", roll);
  
  M5.Lcd.setCursor(0, 80);
  M5.Lcd.printf("Pitch: %.1f", pitch);
  
  M5.Lcd.setCursor(0, 100);
  M5.Lcd.printf("Yaw: %.1f", yaw);
    
  // 運動狀態顯示
  M5.Lcd.setCursor(160, 60);
  M5.Lcd.printf("Curl: %s", motionState ? "YES" : "NO");
  
  // 捲曲計數顯示
  M5.Lcd.setCursor(160, 80);
  M5.Lcd.printf("Count: %d", curlCount);
  
  // 閾值顯示（用於調試）
  M5.Lcd.setCursor(0, 120);
  M5.Lcd.printf("THC:%.1f", ROLL_CURL_THRESHOLD);
  M5.Lcd.setCursor(0, 140);
  M5.Lcd.printf("THR:%.1f", ROLL_RELEASE_THRESHOLD);
}

// ===== 更新狀態顯示 =====
void updateStatus(const char* message, uint16_t color) {
  M5.Lcd.fillRect(0, STATUS_Y, 320, 20, BG_COLOR);
  M5.Lcd.setCursor(0, STATUS_Y);
  M5.Lcd.setTextColor(color, BG_COLOR);
  M5.Lcd.print(message);
  M5.Lcd.setTextColor(TXT_COLOR, BG_COLOR);  // 重置文字顏色
}

// ===== 發送到伺服器 =====
void sendToServer(const String& data) {
  if (client.connected()) {
    client.print(data);  // 向伺服器發送數據
    // In sendToServer function
    M5.Lcd.fillRect(0, 180, 320, 20, BG_COLOR);
    M5.Lcd.setCursor(0, 180);
    M5.Lcd.printf("Sent: %s", data.c_str());
  } else {
    serverConnected = false;
    M5.Lcd.fillRect(0, 180, 320, 20, BG_COLOR);
    M5.Lcd.setCursor(0, 180);
    M5.Lcd.print("Send failed: No connection");
    awaitResponse = false;  // 重置等待標誌
  }
}

// ===== 檢查伺服器回應 =====
void checkServerResponse() {
  // 只有在等待回應時檢查
  if (awaitResponse && client.connected()) {
    // 檢查是否有數據可用
    if (client.available()) {
      String response = client.readStringUntil('\n');
      response.trim();  // 移除多餘空白
      
        // In checkServerResponse function
        M5.Lcd.fillRect(0, 200, 320, 20, BG_COLOR);
        M5.Lcd.setCursor(0, 200);
        M5.Lcd.printf("Server: %s", response.c_str());
      // 處理伺服器回應
      if (response == "OK") {
        // 伺服器確認接收
      } else if (response.startsWith("COUNT:")) {
        // 可能的計數回應（僅作為參考，本地計數器優先）
        String countStr = response.substring(6);
        int serverCount = countStr.toInt();
        // 顯示但不覆蓋本地計數
        M5.Lcd.fillRect(0, 200, 320, 20, BG_COLOR);
        M5.Lcd.setCursor(0, 200);
        M5.Lcd.printf("Server count: %d", serverCount);
      }
      
      awaitResponse = false;  // 收到回應，重置等待標誌
    } else if (millis() - lastSendTime > SERVER_RESPONSE_TIMEOUT) {
      // 超時後無回應，視為超時
      M5.Lcd.fillRect(0, 200, 320, 20, BG_COLOR);
      M5.Lcd.setCursor(0, 200);
      M5.Lcd.print("Server: No response");
      awaitResponse = false;  // 重置等待標誌
    }
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
    
    // 重置按鈕 (0, 240, 106, 40)
    if (pos.x >= 0 && pos.x < 106 && pos.y >= 240 && pos.y < 280) {
      resetCurlCount();
      delay(300); // 防止連續觸發
    } 
    // 校準按鈕 (107, 240, 106, 40)
    else if (pos.x >= 107 && pos.x < 213 && pos.y >= 240 && pos.y < 280) {
      calibrateIMU();
      delay(300); // 防止連續觸發
    }
    // 退出按鈕 (214, 240, 106, 40)
    else if (pos.x >= 214 && pos.x < 320 && pos.y >= 240 && pos.y < 280) {
      // 在實際應用中，可能實現省電模式或退出到選單
      updateStatus("Deep sleep mode", WARN_COLOR);
      delay(1000); // 顯示訊息
      ESP.deepSleep(0); // 進入深度睡眠模式
    }
    // // 測試按鈕 (250, 180, 70, 30)
    // else if (pos.x >= 250 && pos.x < 320 && pos.y >= 180 && pos.y < 210) {
    //   incrementCurlCount();
    //   updateStatus("Test count added", SUCCESS_COLOR);
    //   delay(300); // 防止連續觸發
    // }
    // 閾值調整
    else {
      handleThresholdAdjustment();
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
  
  // 顯示閾值調整指導（左右邊緣）
  M5.Lcd.fillTriangle(0, 70, 10, 60, 10, 80, WARN_COLOR);  // 左上箭頭
  M5.Lcd.fillTriangle(0, 180, 10, 170, 10, 190, WARN_COLOR); // 左下箭頭
  M5.Lcd.fillTriangle(320, 70, 310, 60, 310, 80, WARN_COLOR); // 右上箭頭
  M5.Lcd.fillTriangle(320, 180, 310, 170, 310, 190, WARN_COLOR); // 右下箭頭
  
  // 繪製虛擬按鈕
  M5.Lcd.fillRect(0, 240, 106, 40, BG_COLOR);
  M5.Lcd.drawRect(0, 240, 106, 40, TXT_COLOR);
  M5.Lcd.setCursor(30, 250);
  M5.Lcd.print("Reset");
  
  M5.Lcd.fillRect(107, 240, 106, 40, BG_COLOR);
  M5.Lcd.drawRect(107, 240, 106, 40, TXT_COLOR);
  M5.Lcd.setCursor(115, 250);
  M5.Lcd.print("Calibrate");
  
  M5.Lcd.fillRect(214, 240, 106, 40, BG_COLOR);
  M5.Lcd.drawRect(214, 240, 106, 40, TXT_COLOR);
  M5.Lcd.setCursor(245, 250);
  M5.Lcd.print("Sleep");
}

// 將值從一個範圍映射到另一個範圍
float mapValue(float value, float fromLow, float fromHigh, float toLow, float toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}