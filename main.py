import os
os.environ["OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS"] = "0"
import cv2
import time
import numpy as np
import socket
import tkinter as tk
from tkinter import messagebox
import threading
import sys

# 嘗試使用 tflite_runtime (Jetson環境)，若失敗則使用標準 tensorflow
try:
    import tflite_runtime.interpreter as tflite
    print("Using TFLite Runtime")
except ImportError:
    import tensorflow as tf
    tflite = tf.lite
    print("Using TensorFlow Lite from TensorFlow")

from tools import CustomVideoCapture, preprocess, parse_output

# 全局變數
close_thread = 0
key_in = 0
Curl_Count = 0

# 獲取本機 IP 地址
def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

# 運動次數輸入 GUI
def show_input_dialog():
    global key_in
    
    def button_event():
        global key_in
        tries = spinbox.get()
        if tries:
            tk.messagebox.showinfo('Confirm', f"You want to exercise for {tries} times")
            key_in = int(tries)
            root.destroy()
    
    def validate(P):
        return str.isdigit(P) or P == ''
    
    root = tk.Tk()
    root.title('Exercise Time')
    root.geometry('250x150')
    
    # 顯示伺服器 IP
    ip_addr = get_local_ip()
    tk.Label(root, text='Server IP:', font=("Arial", 14), padx=5, pady=5).grid(row=0, column=0, columnspan=2)
    tk.Label(root, text=ip_addr, font=("Arial", 14), padx=5, pady=5).grid(row=1, column=0, columnspan=2)
    
    # 輸入運動次數
    tk.Label(root, text='Number of times: ', font=("Arial", 14), padx=5, pady=5).grid(row=3, column=0)
    spinbox = tk.Spinbox(from_=0, to=100, width=5, font=("Arial", 14, "bold"))
    spinbox.grid(row=3, column=1)
    
    # 確認按鈕
    tk.Button(root, text='  Enter  ', font=("Arial", 14, "bold"), 
              command=button_event, background='#09c').grid(row=4, column=0, columnspan=2)
    
    root.mainloop()
    return key_in

# 模型推論處理
class ExerciseDetector:
    def __init__(self, model_path="model.tflite"):
        # 加載 TFLite 模型
        self.interpreter = tflite.Interpreter(model_path=model_path)
        self.interpreter.allocate_tensors()
        
        # 獲取輸入輸出細節
        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()
        
        # 模型標籤 - 修改為只有兩個類別，而不是三個
        self.labels = ["Relax", "Move", "Curl"]  # Restore the "Move" class    
    def predict(self, frame):
        # 預處理圖像
        data = preprocess(frame, resize=(224, 224), norm=True)
        
        # 設置輸入張量
        self.interpreter.set_tensor(self.input_details[0]['index'], data)
        
        # 執行推論
        self.interpreter.invoke()
        
        # 獲取預測結果
        prediction = self.interpreter.get_tensor(self.output_details[0]['index'])
        
        # 添加調試信息
        print(f"Prediction shape: {prediction.shape}, values: {prediction}")
        
        # 解析結果 - 添加錯誤處理
        try:
            trg_id, trg_class, trg_prob = parse_output(prediction, self.labels)
            return trg_id, trg_class, trg_prob
        except IndexError as e:
            print(f"Parse error: {e}. Prediction: {prediction}, Labels: {self.labels}")
            # 返回默認值
            return 0, self.labels[0], 0.0

# TCP 伺服器處理
class ExerciseServer:
    def __init__(self, ip=None, port=8080):
        self.ip = ip or get_local_ip()
        self.port = port
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((self.ip, self.port))
        self.server.settimeout(0.1)  # 非阻塞模式
        self.server.listen(10)
        print(f"Server running at {self.ip}:{self.port}")
    
    def accept_client(self):
        try:
            client, addr = self.server.accept()
            client.settimeout(0.5)
            return client
        except socket.timeout:
            return None
    
    def receive_data(self, client):
        try:
            data = client.recv(30)
            if data:
                return str(data, 'utf-8')
            return None
        except:
            return None
    
    def close(self):
        self.server.close()

# 主程式
def main():
    global Curl_Count, close_thread
    
    # 顯示 GUI 並獲取運動次數
    target_count = show_input_dialog()
    if target_count <= 0:
        print("Invalid exercise count. Exiting.")
        return
    
    # 初始化影像擷取
    vid = CustomVideoCapture()
    vid.set_title('Exercise Monitor')
    vid.start_stream()
    
    # 初始化模型推論
    detector = ExerciseDetector()
    
    # 初始化伺服器
    server = ExerciseServer()
    
    # 初始化變數
    case_message = 0        # M5Stack 訊號標記
    case_curl = 0           # 相機捲曲檢測標記
    last_trg_class = ''
    last_count_time = 0     # 上次計數的時間，用於防止重複計數
    
    print(f"Target exercise count: {target_count}")
    
    # 主循環
    while not vid.isStop:
        ret, frame = vid.get_current_frame()
        if not ret:
            continue
        
        current_time = time.time()
        
        # 模型推論
        try:
            trg_id, trg_class, trg_prob = detector.predict(frame)
            print(f"Prediction: {trg_class} ({trg_prob:.2f})")
        except Exception as e:
            print(f"Prediction error: {e}")
            import traceback
            traceback.print_exc()
            continue
        
        # 嘗試接受新客戶端
        client = server.accept_client()
        if client:
            # 接收 MPU6050 消息
            client_message = server.receive_data(client)
            if client_message:
                print(f'M5 Message: {client_message}')
                
                # M5Stack 發送 "1" 時設置標記
                if client_message == '1':
                    case_message = 1
                # M5Stack 發送 "0" 且之前有收到 "1" 時，嘗試增加計數
                elif client_message == '0' and case_message == 1:
                    # 確保距離上次計數至少有1秒，防止重複計數
                    if current_time - last_count_time > 1.0:
                        Curl_Count += 1
                        last_count_time = current_time
                        print(f"Count increased by M5: {Curl_Count}/{target_count}")
                    case_message = 0
        
        # 檢測相機捲曲動作
        if last_trg_class != detector.labels[2] and trg_class == detector.labels[2]:  # 由非Curl變為Curl
            case_curl = 1
            print("Camera detected curl position")
            
        # 從捲曲到放鬆時增加計數
        if case_curl == 1 and last_trg_class == detector.labels[2] and trg_class == detector.labels[0]:  
            # 確保距離上次計數至少有1秒，防止重複計數
            if current_time - last_count_time > 1.0:
                Curl_Count += 1
                last_count_time = current_time
                print(f"Count increased by camera: {Curl_Count}/{target_count}")
            case_curl = 0
        
        # 檢查是否達到目標次數
        if Curl_Count >= target_count:  # 使用 >= 而不是 == 以防止可能的溢出
            print("Exercise completed!")
            time.sleep(1)
            close_thread = 1
            break
        
        # 更新狀態
        last_trg_class = trg_class
        vid.info = f'{trg_class}, Count:{Curl_Count}/{target_count}'
    
    # 結束時顯示完成訊息
    vid.info = f'Finished {Curl_Count} curls. Press Esc to exit.'
    time.sleep(2)
    
    # 關閉資源
    server.close()
    
    # 等待使用者按 ESC 鍵退出
    while not vid.isStop:
        time.sleep(0.1)
    
    print('-' * 30)
    print(f'影像串流的線程是否已關閉: {not vid.t.is_alive()}')
    print('程式結束')

if __name__ == "__main__":
    main()
