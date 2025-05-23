/*
                               居家運動小幫手-電腦輔助判斷彎舉

    ------------------------------------LED Debug--------------------------------------
    |  WIFI -|  DISCONNECT  --> 0.5 sec blink                                         |
    |        |  CONNECT     --> |  Jetson nano server CONNECT     --> keep OFF Light  |
    |                           |  Jetson nano server DISCONNECT  -->  2 sec blink    |
    -----------------------------------------------------------------------------------

    ------------------------------------Revisions--------------------------------------
    |   rev         date        author                    description                 |
    |    0      2023.07.20    Jack Chen       Original version.                       |
    |    1      2023.07.24    Jack Chen       Fix Deley problem.                      |
    |    2      2023.08.08    Jack Chen       Add debug funtion.                      |
    -----------------------------------------------------------------------------------

     -----------------------------------代碼操作----------------------------------------
    |   1.  [27,28行]  更改 Wifi名稱及密碼 27,28行                                      |
    |   2.  [34行]     更改 Jetson nano IP                                             |
    -----------------------------------------------------------------------------------
  
*/

#include <ESP8266WiFi.h>
#include <Wire.h>
#ifndef STASSID
#define STASSID "Biovlsi_111" // Wifi 名稱
#define STAPSK  "1011101110" // Wifi 密碼
#endif

// 設定 Wi-Fi 參數
const char* ssid     = STASSID;
const char* password = STAPSK;
const char* host = "192.168.1.110";  //設定 IP 位置
const uint16_t port = 8080;  // 通訊閘道埠號
int flag=0;

// MPU6050 從裝置地址
const uint8_t MPU6050SlaveAddress = 0x68;

// 選擇 SDA 和 SCL 腳位以進行 I2C 通訊
const uint8_t scl = 12;
const uint8_t sda = 13;

// 校準設定
const uint16_t AccelScaleFactor = 16384;
const uint16_t GyroScaleFactor = 131;

// MPU6050 寄存器地址
const uint8_t MPU6050_REGISTER_SMPLRT_DIV   =  0x19;
const uint8_t MPU6050_REGISTER_USER_CTRL    =  0x6A;
const uint8_t MPU6050_REGISTER_PWR_MGMT_1   =  0x6B;
const uint8_t MPU6050_REGISTER_PWR_MGMT_2   =  0x6C;
const uint8_t MPU6050_REGISTER_CONFIG       =  0x1A;
const uint8_t MPU6050_REGISTER_GYRO_CONFIG  =  0x1B;
const uint8_t MPU6050_REGISTER_ACCEL_CONFIG =  0x1C;
const uint8_t MPU6050_REGISTER_FIFO_EN      =  0x23;
const uint8_t MPU6050_REGISTER_INT_ENABLE   =  0x38;
const uint8_t MPU6050_REGISTER_ACCEL_XOUT_H =  0x3B;
const uint8_t MPU6050_REGISTER_SIGNAL_PATH_RESET  = 0x68;

int16_t AccelX, AccelY, AccelZ, Temperature, GyroX, GyroY, GyroZ;
uint16_t ConnectCheckCount=0;
double Ax=0, Ay=0, Az=0, T=0, Gx=0, Gy=0, Gz=0;
double _Az, _Gz;
int wificheck = 0;

void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);   

  Serial.begin(115200); //通訊頻率
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  //wifi連線狀態檢，如果失敗，開啟閃燈並持續嘗試
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(2, HIGH);  
    delay(250);                     
    digitalWrite(2, LOW);  
    delay(250); 
    Serial.println("Check WiFi");
  }
  Serial.println("WiFi connected");
  Wire.begin(sda, scl);
  MPU6050_Init();
}


void loop() {
  static WiFiClient client;
  
  //每10000次數據讀取後檢查連線一，降低系統負擔
  if(wificheck = 10000){
  while (!client.connect(host, port)) {
    Serial.println("Jetson Nano connection failed");
    digitalWrite(2, LOW);                                                     // 與Jetson Nano 連線失敗會開啟燈
    }
  wificheck = 0;
  }
  
  digitalWrite(2, HIGH);                                                     //順利通過檢查燈會熄滅
  wificheck++;

  Read_RawValue(MPU6050SlaveAddress, MPU6050_REGISTER_ACCEL_XOUT_H);

  _Az = Az;                                                                  // 儲存上一次的加速度計 Z 軸數值
  _Gz = Gz;                                                                  // 儲存上一次的陀螺儀 Z 軸數值
  Ay = (double)AccelY/AccelScaleFactor;                                      // 將加速度計 Y 軸數值轉換為實際加速度
  Az = (double)AccelZ/AccelScaleFactor;                                      // 將加速度計 Z 軸數值轉換為實際加速度
  Gx = (double)GyroX/GyroScaleFactor;                                        // 將陀螺儀 X 軸數值轉換為實際角速度
  Gz = (double)GyroZ/GyroScaleFactor;                                        // 將陀螺儀 Z 軸數值轉換為實際角速度
  
  //判定動作
  if ((Gx > 80) && (Az - _Az) > 0 && (Gz - _Gz) < -10){                      // 判斷是否捲曲動作
      SentToServer(client,String("0"));                                      // 將捲曲動作狀態（"0"）傳送至伺服器
      flag = 1;                                                              // 設定標誌位以指示捲曲狀態
    }
  else if ((Gx < 60) && (Gz - _Gz) > 5 && Ay < 0.5 && flag == 1){            // 判斷是否放鬆動作
      SentToServer(client,String("1"));                                      // 將放鬆動作狀態（"1"）傳送至伺服器
      flag = 0;                                                              // 重設標誌位以指示非捲曲狀態
    }
  else  SentToServer(client,String("0"));                                    // 其他情況，將狀態（"0"）傳送至伺服器
       Serial.printf(" flag: %d ", flag);
}
   
// 傳送資料到 Jetson nano
void SentToServer(WiFiClient client, String str){
    client.print(str);                                                       // 將資料傳送至伺服器
    Serial.println(" Sent: " + str);                                         // 在序列監視器中顯示已傳送的資料
}

// 進行 I2C 寫入
void I2C_Write(uint8_t deviceAddress, uint8_t regAddress, uint8_t data){
  Wire.beginTransmission(deviceAddress);                                     // 開始 I2C 通訊，指定裝置地址
  Wire.write(regAddress);                                                    // 寫入寄存器地址
  Wire.write(data);                                                          // 寫入要寫入的數據
  Wire.endTransmission();                                                    // 結束 I2C 通訊
}

// Read all 14 register
void Read_RawValue(uint8_t deviceAddress, uint8_t regAddress){
  Wire.beginTransmission(deviceAddress);                                     // 開始 I2C 通訊，指定裝置地址
  Wire.write(regAddress);                                                    // 寫入要讀取的寄存器地址
  Wire.endTransmission();                                                    // 結束 I2C 通訊
  Wire.requestFrom(deviceAddress, (uint8_t)14);                              // 請求從裝置地址讀取 14 個字節的數據
  AccelX = (((int16_t)Wire.read()<<8) | Wire.read());                        // 讀取加速度計 X 軸數據
  AccelY = (((int16_t)Wire.read()<<8) | Wire.read());                        // 讀取加速度計 Y 軸數據
  AccelZ = (((int16_t)Wire.read()<<8) | Wire.read());                        // 讀取加速度計 Z 軸數據
  Temperature = (((int16_t)Wire.read()<<8) | Wire.read());                   // 讀取溫度數據
  GyroX = (((int16_t)Wire.read()<<8) | Wire.read());                         // 讀取陀螺儀 X 軸數據
  GyroY = (((int16_t)Wire.read()<<8) | Wire.read());                         // 讀取陀螺儀 Y 軸數據
  GyroZ = (((int16_t)Wire.read()<<8) | Wire.read());                         // 讀取陀螺儀 Z 軸數據
}

// 設定 MPU6050 初始化
void MPU6050_Init(){
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_SMPLRT_DIV, 0x07);         // 設定採樣率分頻器
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_PWR_MGMT_1, 0x01);         // 啟用 MPU6050，設定時鐘源
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_PWR_MGMT_2, 0x00);         // 關閉休眠模式
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_CONFIG, 0x00);             // 配置內部低通濾波器
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_GYRO_CONFIG, 0x00);        // 設定陀螺儀濾波器和範圍
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_ACCEL_CONFIG, 0x00);       // 設定加速度計範圍
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_FIFO_EN, 0x00);            // 關閉 FIFO
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_INT_ENABLE, 0x01);         // 啟用中斷
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_SIGNAL_PATH_RESET, 0x00);  // 重置信號路徑
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_USER_CTRL, 0x00);          // 關閉附加功能
}
