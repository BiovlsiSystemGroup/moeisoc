# 智慧晶片系統與應用人才培育計畫 
## 第三代 - 居家運動助手系統
- [Jetson Orin Nano 教學手冊 (PDF)](https://github.com/BiovlsiSystemGroup/moeisoc/releases/download/v3.0.0/Jetson.orin.nano._20250813.pdf)
## 專案簡介
本專案是一個基於 AI 視覺識別和 IoT 設備的居家運動監測系統，結合了以下技術：
- **TensorFlow Lite** 模型進行即時運動姿態識別
- **M5Stack Core2** ESP32 IoT 設備作為運動感測器
- **Flask** Web API 服務接收 IoT 數據
- **Tkinter** GUI 界面顯示運動計數和狀態
- **OpenCV** 影像處理和相機串流

## 主要功能
### 1. 雙重運動檢測
- **相機視覺檢測**：使用 TFLite 模型識別運動狀態（Relax、Move、Curl）
- **IoT 感測檢測**：透過 M5Stack 設備檢測運動次數

### 2. 即時監控界面
- 圖形化 GUI 顯示兩種檢測方式的計數
- 即時狀態顯示（Relax/Move/Curl）
- 加權計算顯示（取兩個檢測值的最小值）
- 一鍵重置計數功能

### 3. Web API 服務
- HTTP 服務器接收 M5Stack 設備數據
- RESTful API 提供系統狀態查詢
- 支援遠程監控和數據交換

### 4. 計數邏輯
- 防重複計數機制（1秒間隔限制）
- 狀態變化檢測（從 Curl 到 Relax 才計數）
- 多線程處理確保系統穩定性

## 硬體設備
- NVIDIA Jetson Orin Nano Super Developer Kit (67 TOPS, 8GB, 25W)
- M5Stack Core2 ESP32 IoT Development Kit V1.1 開發套件

## 自備材料
- 皿頭螺絲 M3 14mm (使用2號六角板手)*2
- 3D列印手錶支架+綁帶

## 周邊設備
- MicroSD 卡 64GB (A1等級以上)
- C270 webcam
- DP轉HDMI/VGA/DVI
- (非必要) Crucial P3 Plus 500GB M.2 2280 PCIe 4.0
- (非必要) USB-A TO Type-C 傳輸線
- (非必要) Jetson Orin Nano 壓克力外殼


## 系統需求
### 軟體環境
- 請參閱教學文件



## 檔案結構
```
moeisoc/
├── main.py              # 主程式
├── tools.py             # 工具函數（影像處理、模型工具）
├── model.tflite         # TensorFlow Lite 模型檔案
├── keras_model.h5       # Keras 模型檔案
├── labels.txt           # 類別標籤檔案
├── README.md            # 專案說明文件
├── yuntech_logo.png     # 雲科大 Logo
├── Anduino_V3/          # Arduino/M5Stack 程式碼
│   └── Anduino_V3.ino
└── Case_3D_File_V3/     # 3D 列印檔案
    └── M5StackCore2Watch.stl
```

## 使用方法
### 1. 啟動系統
```bash
python main.py
```

### 2. 系統啟動後會自動：
- 啟動相機串流視窗
- 開啟 Tkinter GUI 監控界面
- 啟動 Flask Web 服務器（預設 port 5000）
- 顯示本機 IP 位址供 M5Stack 連接

### 3. GUI 操作
- **即時顯示**：M5Stack 計數、TFLite 計數、當前狀態
- **加權計算**：顯示兩個檢測值的最小值
- **Reset Counts**：重置所有計數為零
- **關閉視窗**：停止所有服務並結束程式

### 4. 快捷鍵操作
- **空白鍵**：重置計數
- **ESC鍵**：退出程式
- **X按鈕**：關閉 GUI 視窗

## API 端點
### POST /count
接收 M5Stack 設備的計數數據
```json
{
  "count": 15
}
```

### GET /status
獲取當前系統狀態
```json
{
  "tflite_count": 10,
  "m5stack_count": 12,
  "total_count": 22
}
```

## M5Stack 配置
M5Stack 設備需要發送 HTTP POST 請求到：
```
http://[電腦IP]:5000/count
```

程式啟動時會自動顯示正確的 IP 位址。

## 技術特色
### 1. AI 模型整合
- 支援 TensorFlow 和 TFLite Runtime 自動選擇
- 224x224 輸入解析度的運動姿態識別
- 三類別分類：Relax（休息）、Move（移動）、Curl（捲曲）

### 2. 多線程架構
- **主線程**：Tkinter GUI 界面
- **檢測線程**：影像處理和 AI 推論
- **Flask線程**：Web API 服務
- **影像線程**：相機串流處理

### 3. 穩定性設計
- 異常處理和錯誤恢復機制
- 防重複計數邏輯（1秒冷卻時間）
- 線程安全的狀態更新
- 優雅的程式關閉流程

### 4. 跨平台支援
- Windows/Linux/macOS 相容
- NVIDIA Jetson 硬體加速支援
- 自動偵測和配置最佳運行環境

## 故障排除
### 相機無法開啟
```python
# 在 main.py 開頭已設定，解決 Windows MSMF 問題
os.environ["OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS"] = "0"
```

### TFLite 模型載入失敗
- 確認 `model.tflite` 檔案存在於專案目錄
- 檢查 TensorFlow 或 TFLite Runtime 是否正確安裝

### M5Stack 無法連接
- 確認電腦和 M5Stack 在同一網路
- 檢查防火牆設定是否允許 port 5000
- 確認程式顯示的 IP 位址正確

## 開發團隊
國立雲林科技大學 電子工程系
跨領域系統暨生醫應用設計實驗室&智慧醫療研究中心

## 版本歷史
- **V3.0**：目前版本，整合 AI 視覺識別與 IoT 雙重檢測
- 支援即時監控、Web API 服務、圖形化界面
