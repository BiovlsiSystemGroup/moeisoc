# 客製化的影像擷取程式
import cv2
import threading
import os, time, random
import numpy as np
import tensorflow as tf
import platform as plt

class CustomVideoCapture():

    # 初始化 預設的攝影機裝置為 0
    def __init__(self, dev=0):

        self.cap = cv2.VideoCapture(dev)
        self.ret = ''
        self.frame = []
        self.win_title = 'Modified with set_title()'
        self.info = ''
        self.fps = 0
        self.fps_time = 0

        self.isStop = False
        self.t = threading.Thread(target=self.video, name='stream')
    
    # 可以透過這個函式 開啟 Thread 
    def start_stream(self):
        self.t.start()
    
    
    # 關閉 Thread 與 Camera
    def stop_stream(self):
        self.isStop = True
        self.cap.release()
        cv2.destroyAllWindows()

    
    # 取得最近一次的幀
    def get_current_frame(self):
        return self.ret, self.frame

    # 取得幀率
    def get_fps(self):
        return self.fps

    
    # 設定顯示視窗的名稱
    def set_title(self, txt):
        self.win_title = txt
    
    # Thread主要運行的函式
    def video(self):
        try:
            global close_thread
            close_thread=0
            while(not self.isStop):
                self.fps_time = time.time()
                self.ret, self.frame = self.cap.read()
                self.frame=cv2.flip(self.frame,1)

                if self.info is not '':
                    cv2.putText(self.frame, self.info, (10,40), cv2.FONT_HERSHEY_SIMPLEX, 1, (0,0,255), 2)
                cv2.namedWindow('Biovlsi')
                cv2.imshow('Biovlsi', self.frame)
                cv2.resizeWindow('Biovlsi',1280,960)
                
                if cv2.waitKey(1) == 27:
                    break
                if close_thread == 1:
                    break
                self.fps = int(1/(time.time() - self.fps_time))

            self.stop_stream()
        except:
            self.stop_stream()

# 用於資料前處理的程式
def preprocess(frame, resize=(224, 224), norm=True):
    '''
    設定格式 ( 1, 224, 224, 3)、縮放大、正規化、放入資料並回傳正確格式的資料
    '''
    input_format = np.ndarray(shape=(1, 224, 224, 3), dtype=np.float32)
    frame_resize = cv2.resize(frame, resize)
    frame_norm =  ((frame_resize.astype(np.float32) / 127.0) - 1) if norm else frame_resize
    input_format[0]=frame_norm
    return input_format
            # 讀取 模型 與 標籤


# 讀取續練集
def load_engine(engine_path):

    if trt_found:

        TRT_LOGGER = trt.Logger(trt.Logger.WARNING)
        trt_runtime = trt.Runtime(TRT_LOGGER)
        
        with open(engine_path, 'rb') as f:
            engine_data = f.read()
        engine = trt_runtime.deserialize_cuda_engine(engine_data)

        return engine
    else:
        print("Can not load load_engine because there is no tensorrt module")
        exit(1)

# 解析輸出資訊
def parse_output(preds, label) -> 'return ( class id, class name, probobility) ':
    
    preds = preds[0] if len(preds.shape)==4 else preds
    trg_id = np.argmax(preds)
    trg_name = label[trg_id]
    trg_prob = preds[trg_id]
    return ( trg_id, trg_name, trg_prob)
