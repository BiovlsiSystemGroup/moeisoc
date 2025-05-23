#include <ESP8266WiFi.h>
#include <Wire.h>

#ifndef STASSID
#define STASSID "Biovlsi_111" // Wifi number
#define STAPSK  "1011101110" // Wifi password
#endif

// Set Wifi parameters
const char* ssid     = STASSID;
const char* password = STAPSK;
const char* host = "192.168.1.103";  //Set IP location
const uint16_t port = 8080;  // communication gateway
int flag=0;

// MPU6050 Slave Device Address
const uint8_t MPU6050SlaveAddress = 0x68;

// Select SDA and SCL pins to communicate with I2C
const uint8_t scl = 12;
const uint8_t sda = 13;

// Sensitivity scale factors are provided for full-scale settings in the datasheet
const uint16_t AccelScaleFactor = 16384;
const uint16_t GyroScaleFactor = 131;

// MPU6050 few configure register address
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
double _Az, _Gz; //Last obtained data

void setup() {
  Serial.begin(115200); //Communication frequency
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(5000);
    Serial.print("Check WiFi");
  } 
  Wire.begin(sda, scl);
  MPU6050_Init();
}

void loop() {
  static WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    delay(500);
    return;
  }
  Read_RawValue(MPU6050SlaveAddress, MPU6050_REGISTER_ACCEL_XOUT_H);
 
 //divide each with their sensitivity scale factor
  _Az = Az;
  _Gz = Gz;
  Ay = (double)AccelY/AccelScaleFactor;
  Az = (double)AccelZ/AccelScaleFactor;
  Gx = (double)GyroX/GyroScaleFactor;
  Gz = (double)GyroZ/GyroScaleFactor;
  
  //Set the curl judgment threshold and the value sent to Jetson nano
  if ((Gx > 80) && (Az - _Az) > 0 && (Gz - _Gz) < -10){ // Curl
      SentToServer(client,String("0"));
      flag = 1;
    }
  else if ((Gx < 60) && (Gz - _Gz) > 5 && Ay < 0.5 && flag == 1){ // Relax
      SentToServer(client,String("1"));
      flag = 0;
    }
  
    else  SentToServer(client,String("0"));
       Serial.printf(" flag: %d ", flag);
    }
   
//Sent to Jetson nano
void SentToServer(WiFiClient client, String str){
    client.print(str);
    Serial.println(" Sent: " + str);
    delay(10);
}

void I2C_Write(uint8_t deviceAddress, uint8_t regAddress, uint8_t data){
  Wire.beginTransmission(deviceAddress);
  Wire.write(regAddress);
  Wire.write(data);
  Wire.endTransmission();
}

// Read all 14 register
void Read_RawValue(uint8_t deviceAddress, uint8_t regAddress){
  Wire.beginTransmission(deviceAddress);
  Wire.write(regAddress);
  Wire.endTransmission();
  Wire.requestFrom(deviceAddress, (uint8_t)14);
  AccelX = (((int16_t)Wire.read()<<8) | Wire.read());
  AccelY = (((int16_t)Wire.read()<<8) | Wire.read());
  AccelZ = (((int16_t)Wire.read()<<8) | Wire.read());
  Temperature = (((int16_t)Wire.read()<<8) | Wire.read());
  GyroX = (((int16_t)Wire.read()<<8) | Wire.read());
  GyroY = (((int16_t)Wire.read()<<8) | Wire.read());
  GyroZ = (((int16_t)Wire.read()<<8) | Wire.read());
}

// configure MPU6050
void MPU6050_Init(){
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_SMPLRT_DIV, 0x07);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_PWR_MGMT_1, 0x01);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_PWR_MGMT_2, 0x00);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_CONFIG, 0x00);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_GYRO_CONFIG, 0x00);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_ACCEL_CONFIG, 0x00);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_FIFO_EN, 0x00);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_INT_ENABLE, 0x01);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_SIGNAL_PATH_RESET, 0x00);
  I2C_Write(MPU6050SlaveAddress, MPU6050_REGISTER_USER_CTRL, 0x00);
}
