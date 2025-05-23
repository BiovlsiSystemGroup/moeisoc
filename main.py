import cv2
import threading
import os, time, random
import numpy as np
import tensorflow as tf
import platform as plt
from tools import CustomVideoCapture, preprocess, parse_output
import time
import socket
import tkinter as tk
from tkinter import messagebox

##輸入GUI
def button_event():
    global key_in
    print(myentry.get())
    while myentry.get():
        tk.messagebox.showinfo('Confirm',"You want to exercise for %s times" %(myentry.get()))
        break
    key_in = int(myentry.get())

def validate(P):
    #print(P)
    if str.isdigit(P) or P == '':
        return True
    else:
        return False
       
root = tk.Tk()
root.title('Exercise Time')
root.geometry('250x50')

mylabel = tk.Label(root, text='Number of times')
mylabel.grid(row=0, column=0)

vcmd = (root.register(validate), '%P')
myentry = tk.Entry(root, validate='key', validatecommand=vcmd)
myentry.grid(row=0, column=1)

mybutton = tk.Button(root, text='Enter', command=button_event)
mybutton.grid(row=1, column=0)

mybutton2 = tk.Button(root, text='Exit', command=root.destroy)
mybutton2.grid(row=1, column=1)


root.mainloop()
#t_end = time.time() + key_in
##

# label與model來源
label=["Relax","Move","Curl"] 
model=tf.keras.models.load_model("keras_model.h5",compile=False)

# 設定伺服器IP
HOST = '140.125.35.83'
PORT = 8080
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind((HOST, PORT))
server.listen(10)

# 給予相關參數

global Curl_Count
Curl_Count=0
case_message=0
case_curl=0
finish_curl=0
last_trg_class=''
# 設定影像擷取
vid = CustomVideoCapture()
vid.set_title('Biovlsi')
vid.start_stream()

# 設定幾秒辨識一次，降低運行負擔
t_check = 0
t_delay = 0
t_start = 0

# 開始即時辨識
t_start = time.time()
while(not vid.isStop):
        '''t_check = time.time() - t_start'''            
# 取得當前圖片
        ret, frame = vid.get_current_frame()        
# 如果沒有幀則重新執行
        if not ret: continue        
# 進行處理與推論
        data = preprocess(frame, resize=(224,224), norm=True)
        prediction = model(data)[0]
# 進行client資料抓取
        client, addr =server.accept()
        clientMessage=str(client.recv(30),'utf-8')
# 解析 辨識結果
        #last_trg_class=trg_class
        trg_id, trg_class, trg_prob =parse_output(prediction, label)
        
# 設定顯示資訊
        print('MPU6050_Message:'+clientMessage)
# 進行NANO及MPU6050同時比對
        if (clientMessage=='1'):
            case_message=1
        if(last_trg_class==label[2]):
            case_curl=1
        if(case_curl==1 and trg_class==label[0]):
            finish_curl=1
            case_curl=0
        if (case_message==1 and finish_curl==1):
            #global Curl_Count
            Curl_Count=Curl_Count+1
            case_message=0
            finish_curl=0
        if Curl_Count == key_in:
            print("finish curl")
            time.sleep(2)
            close_thread=1
            break
        last_trg_class=trg_class
        vid.info = '{} ,Count:{} '.format(trg_class,Curl_Count)#, vid.get_fps())
# 更新 time

        t_start = time.time()
vid.info =('Finish %s times curls Please press Esc' %Curl_Count)
while(vid.isStop):
    root1 = tk.Tk()
    root1.title('Congratulation')
    root1.geometry('500x500')
    mylabel1 = tk.Label(root1, text='You finish %s times curls' %key_in, fg='red', bd=200, cursor = 'cross', font=('Times New Roman',30)).pack()
    mybutton2 = tk.Button(root1, text='OK', command=root1.destroy).pack()
    root1.mainloop()
# 跳出 while 迴圈需要檢查多線程是否已經關閉
time.sleep(1)
print(close_thread)
print('-'*30)
print(f'影像串流的線程是否已關閉 : {not vid.t.is_alive()}')
print('離開程式')
