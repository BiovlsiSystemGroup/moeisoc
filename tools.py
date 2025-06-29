import cv2
import threading
import os
import time
import numpy as np
import platform as plt

# 全局變數用於按鍵檢測
reset_counts = False

class CustomVideoCapture():
    def __init__(self, dev=0):
        self.cap = cv2.VideoCapture(dev)
        self.ret = False
        self.frame = None     
        self.win_title = 'Modified with set_title()'
        self.fps = 0
        self.fps_time = 0
        self.isStop = False
        self.t = threading.Thread(target=self.video, name='stream')

    def start_stream(self):
        self.t.start()
    
    def stop_stream(self):
        self.isStop = True
        self.cap.release()
        cv2.destroyAllWindows()

    def get_current_frame(self):
        return self.ret, self.frame

    def get_fps(self):
        return self.fps
    
    def set_title(self, txt):
        self.win_title = txt

    def check_reset_flag(self):
        global reset_counts
        if reset_counts:
            reset_counts = False
            return True
        return False

    def video(self):
        try:
            global close_thread
            close_thread = 0

            frame_rate = int(self.cap.get(cv2.CAP_PROP_FPS))
            frame_width = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            frame_height = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            
            # 檢查 logo 檔案是否存在
            image_path = 'yuntech_logo.png'
            if os.path.exists(image_path):
                another_image = cv2.imread(image_path)
                new_size = (100, 100)
                resized_another_image = cv2.resize(another_image, new_size)
            else:
                resized_another_image = None
                print(f"Warning: {image_path} not found")

            while not self.isStop:
                self.fps_time = time.time()
                self.ret, self.frame = self.cap.read()
                
                if not self.ret:
                    # 如果讀取失敗，暫停一下再試
                    time.sleep(0.1)
                    continue
                    
                self.frame = cv2.flip(self.frame, 1)
                
                # 只在 logo 存在時添加 logo
                if resized_another_image is not None:
                    x_offset = frame_width - new_size[1]
                    y_offset = frame_height - new_size[0]
                    self.frame[y_offset:frame_height, x_offset:frame_width] = resized_another_image

                # Calculate FPS
                fps_text = f"FPS: {self.fps}"
                cv2.putText(self.frame, fps_text, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

                cv2.namedWindow(self.win_title)
                cv2.imshow(self.win_title, self.frame)
                cv2.resizeWindow(self.win_title, 640, 480)

                key = cv2.waitKey(1) & 0xFF
                if key == 27:  # ESC key
                    break
                elif key == ord(' '):  # Space key for reset
                    global reset_counts
                    reset_counts = True
                    
                if close_thread == 1:
                    break
                    
                self.fps = int(1 / (time.time() - self.fps_time))

            self.stop_stream()
        except Exception as e:
            print(f"Video stream error: {e}")
            self.stop_stream()

def preprocess(frame, resize=(224, 224), norm=True):
    '''
    設定格式 (1, 224, 224, 3)、縮放大、正規化、放入資料並回傳正確格式的資料
    '''
    input_format = np.ndarray(shape=(1, resize[0], resize[1], 3), dtype=np.float32)
    frame_resize = cv2.resize(frame, resize)
    frame_norm = ((frame_resize.astype(np.float32) / 127.0) - 1) if norm else frame_resize
    input_format[0] = frame_norm
    return input_format

def load_engine(engine_path):
    try:
        import tensorrt as trt
        TRT_LOGGER = trt.Logger(trt.Logger.WARNING)
        trt_runtime = trt.Runtime(TRT_LOGGER)
        
        with open(engine_path, 'rb') as f:
            engine_data = f.read()
        engine = trt_runtime.deserialize_cuda_engine(engine_data)
        return engine
    except ImportError:
        print("TensorRT module not found. Cannot load engine.")
        return None
    except Exception as e:
        print(f"Error loading TensorRT engine: {e}")
        return None

def parse_output(preds, label):
    '''
    解析輸出資訊
    return ( class id, class name, probability)
    '''
    preds = preds[0] if len(preds.shape) > 1 else preds
    
    # Find class with highest probability
    trg_id = np.argmax(preds)
    
    # Map to our available labels
    if trg_id >= len(label):
        # If the highest probability class doesn't have a label, map it
        # In this case, we're mapping class 2 (index value) to "Curl" (index 1 in our labels)
        if trg_id == 2:  # This is the "Curl" class in the original model
            trg_id = 1   # Map to index 1 which is "Curl" in our labels
        else:
            trg_id = 0   # Default to "Relax" for other cases
    
    trg_name = label[trg_id]
    trg_prob = preds[trg_id]
    return (trg_id, trg_name, trg_prob)