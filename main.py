import os
os.environ["OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS"] = "0"
import cv2
import time
import numpy as np
import socket
import threading
import sys
import tkinter as tk
from flask import Flask, request, jsonify

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
TFLite_Count = 0    # TFLite模型檢測的次數
M5Stack_Count = 0   # M5Stack檢測的次數

# Tkinter GUI 類別
class CounterGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Exercise Monitor")
        self.root.geometry("400x350")
        self.root.configure(bg='#f0f0f0')
        
        # 建立標題
        title_label = tk.Label(self.root, text="Home Exercise Assistant", 
                      font=("Arial", 16, "bold"), bg='#f0f0f0')
        title_label.pack(pady=10)
        
        # 建立顯示框架
        display_frame = tk.Frame(self.root, bg='#f0f0f0')
        display_frame.pack(pady=10)
        
        # M5Stack 計數顯示
        self.m5stack_label = tk.Label(display_frame, text="M5Stack: 0", 
                                     font=("Arial", 12), bg='#e6f3ff', 
                                     relief="solid", padx=10, pady=5)
        self.m5stack_label.grid(row=0, column=0, padx=5, pady=5)
        
        # TFLite 計數顯示
        self.tflite_label = tk.Label(display_frame, text="TFLite: 0", 
                                    font=("Arial", 12), bg='#ffe6e6', 
                                    relief="solid", padx=10, pady=5)
        self.tflite_label.grid(row=0, column=1, padx=5, pady=5)
        
        # 狀態顯示
        self.status_label = tk.Label(display_frame, text="Status: Relax", 
                                    font=("Arial", 12), bg='#e6ffe6', 
                                    relief="solid", padx=10, pady=5)
        self.status_label.grid(row=1, column=0, columnspan=2, padx=5, pady=5)
        
        # 加權計算顯示
        self.weighted_label = tk.Label(display_frame, text="Weighted calculation: 0", 
                                      font=("Arial", 12), bg='#fff2e6', 
                                      relief="solid", padx=10, pady=5)
        self.weighted_label.grid(row=2, column=0, columnspan=2, padx=5, pady=5)
        
        # Reset 按鈕
        self.reset_button = tk.Button(self.root, text="Reset Counts", 
                                     font=("Arial", 12, "bold"), 
                                     bg='#ff6b6b', fg='white',
                                     command=self.reset_counts,
                                     padx=20, pady=10)
        self.reset_button.pack(pady=20)
        
        # 指示說明
        instruction_label = tk.Label(self.root, text="Press X in window to exit", 
                                   font=("Arial", 10), bg='#f0f0f0', fg='#666')
        instruction_label.pack(pady=5)
        
        # 設定關閉事件
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        
    def update_display(self, m5stack_count, tflite_count, status):
        """更新顯示的計數和狀態"""
        self.m5stack_label.config(text=f"M5Stack: {m5stack_count}")
        self.tflite_label.config(text=f"TFLite: {tflite_count}")
        self.status_label.config(text=f"Status: {status}")
        
        # 計算加權計算
        #
        # 取兩個數據的最小值作為加權計算的結果 
        #
        weighted_value = min(m5stack_count, tflite_count)
        self.weighted_label.config(text=f"Weighted calculation: {weighted_value}")
        
    def reset_counts(self):
        """重置計數的回調函數"""
        global TFLite_Count, M5Stack_Count
        TFLite_Count = 0
        M5Stack_Count = 0
        print("Counts reset to zero via GUI button")
        
    def on_closing(self):
        """視窗關閉事件"""
        global close_thread
        close_thread = 1
        self.root.destroy()
        
    def start_gui(self):
        """啟動GUI主循環"""
        self.root.mainloop()

# Flask應用初始化
app = Flask(__name__)

# Flask路由：接收M5Stack的計數
@app.route('/count', methods=['POST'])
def receive_count():
    global M5Stack_Count
    try:
        data = request.json
        if data and 'count' in data:
            M5Stack_Count = data.get('count')
 
            print(f"Total M5Stack count: {M5Stack_Count}")
            return jsonify({'status': 'ok', 'total_count': M5Stack_Count}), 200
        else:
            print("Invalid data received from M5Stack")
            return jsonify({'status': 'error', 'message': 'Invalid data'}), 400
    except Exception as e:
        print(f"Error processing M5Stack request: {e}")
        return jsonify({'status': 'error', 'message': str(e)}), 500

# Flask路由：獲取當前計數
@app.route('/status', methods=['GET'])
def get_status():
    return jsonify({
        'tflite_count': TFLite_Count,
        'm5stack_count': M5Stack_Count,
        'total_count': TFLite_Count + M5Stack_Count
    }), 200

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

# Flask服務器運行函數
def run_flask_server():
    """在後台線程中運行Flask服務器"""
    app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)

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

# 主程式
def main():
    global TFLite_Count, M5Stack_Count, close_thread
    
    local_ip = get_local_ip()
    print(f"Starting Flask server at: http://{local_ip}:5000")
    print(f"M5Stack should send POST requests to: http://{local_ip}:5000/count")
    print("Press SPACE to reset counts, ESC to exit")
    
    # 在後台線程啟動Flask服務器
    flask_thread = threading.Thread(target=run_flask_server, daemon=True)
    flask_thread.start()
    print("Flask server started in background thread")
    
    # 初始化Tkinter GUI
    gui = CounterGUI()
    
    # 初始化影像擷取
    vid = CustomVideoCapture()
    vid.set_title('Exercise Monitor - Camera View')
    vid.start_stream()
    
    # 初始化模型推論
    detector = ExerciseDetector()
    
    # 初始化變數
    last_trg_class = ''
    last_count_time = 0     # 上次計數的時間，用於防止重複計數
    camera_curl_state = False  # 相機檢測到curl狀態
    
    print("Exercise detection started...")
    
    # 在後台線程運行主檢測循環
    def detection_loop():
        global TFLite_Count, M5Stack_Count, close_thread
        nonlocal last_trg_class, last_count_time, camera_curl_state
        
        while not vid.isStop and close_thread == 0:
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
            
            # 檢測TFLite相機捲曲動作 - 獨立計數
            if last_trg_class != detector.labels[2] and trg_class == detector.labels[2]:  # 進入Curl狀態
                camera_curl_state = True
                print("Camera detected curl position")
                
            # 從捲曲到放鬆時增加TFLite計數
            if camera_curl_state and last_trg_class == detector.labels[2] and trg_class == detector.labels[0]:  
                # 確保距離上次計數至少有1秒，防止重複計數
                if current_time - last_count_time > 1.0:
                    TFLite_Count += 1
                    last_count_time = current_time
                    print(f"TFLite count increased: {TFLite_Count}")
                camera_curl_state = False
            
            # 檢查是否按了空白鍵重置計數
            if vid.check_reset_flag():
                TFLite_Count = 0
                M5Stack_Count = 0
                print("Counts reset to zero")
            
            # 更新狀態
            last_trg_class = trg_class
            
            # 更新GUI顯示（在主線程中安全地更新）
            try:
                gui.root.after(0, lambda: gui.update_display(M5Stack_Count, TFLite_Count, trg_class))
            except:
                pass  # GUI可能已經關閉
            
            time.sleep(0.1)  # 減少CPU使用率
    
    # 啟動檢測線程
    detection_thread = threading.Thread(target=detection_loop, daemon=True)
    detection_thread.start()
    
    # 啟動GUI主循環（這會阻塞直到GUI關閉）
    gui.start_gui()
    
    # GUI關閉後，停止所有線程
    close_thread = 1
    vid.stop_stream()
    
    print('-' * 30)
    print(f'TFLite總計數: {TFLite_Count}')
    print(f'M5Stack總計數: {M5Stack_Count}')
    print(f'影像串流的線程是否已關閉: {not vid.t.is_alive()}')
    print('程式結束')

if __name__ == "__main__":
    main()
