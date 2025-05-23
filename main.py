import cv2
import threading
import os
import time
import random
import numpy as np
import tflite_runtime.interpreter as tflite
import socket
import tkinter as tk
from tkinter import messagebox
from tools import CustomVideoCapture, preprocess, parse_output

# Create a UDP socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(("8.8.8.8", 80))
IPAddr = s.getsockname()[0]

# Input GUI
def button_event():
    global key_in
    print(spinbox.get())
    while spinbox.get():
        tk.messagebox.showinfo('Confirm', "You want to exercise for %s times" % (spinbox.get()))
        break
    key_in = int(spinbox.get())
    root.destroy()

def validate(P):
    if str.isdigit(P) or P == '':
        return True
    else:
        return False
       
def spinbox_used():
    print(spinbox.get())

root = tk.Tk()
root.title('Exercise Time')
root.geometry('250x150')

mylabel1 = tk.Label(root, text='Server IP:', font=("Arial", 14), padx=5, pady=5)
mylabel1.grid(row=0, column=0, columnspan=2)
mylabel2 = tk.Label(root, text=IPAddr, font=("Arial", 14), padx=5, pady=5)
mylabel2.grid(row=1, column=0, columnspan=2)

mylabel3 = tk.Label(root, text='Number of times: ', font=("Arial", 14), padx=5, pady=5)
mylabel3.grid(row=3, column=0)
spinbox = tk.Spinbox(from_=0, to=100, width=5, font=("Arial", 14, "bold"), command=spinbox_used)
spinbox.grid(row=3, column=1)
mybutton = tk.Button(root, text='  Enter  ', font=("Arial", 14, "bold"), command=button_event, background='#09c')
mybutton.grid(row=4, column=0, columnspan=2)

root.mainloop()

# Load the TensorFlow Lite model and allocate tensors using tflite_runtime
interpreter = tflite.Interpreter(model_path="model.tflite")
interpreter.allocate_tensors()

# Get input and output details
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# Labels
labels = ["Relax", "Move", "Curl"]

PORT = 8080
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind((IPAddr, PORT))
server.listen(10)
print("Your Computer IP Address is:" + IPAddr)

global Curl_Count
Curl_Count = 0
case_message = 0
case_curl = 0
finish_curl = 0
last_trg_class = ''

# Video Capture
vid = CustomVideoCapture()
vid.set_title('Biovlsi') 
vid.start_stream()

t_check = 0
t_delay = 0
t_start = 0

# Real-time recognition
t_start = time.time()
while not vid.isStop:
    ret, frame = vid.get_current_frame()
    if not ret:
        continue
    
    # Preprocess the image for the TensorFlow Lite model
    data = preprocess(frame, resize=(224, 224), norm=True)

    # Verify the shape
    print(f"Preprocessed data shape: {data.shape}")

    # Set the tensor
    interpreter.set_tensor(input_details[0]['index'], data)

    # Run inference
    interpreter.invoke()

    # Get the prediction result
    prediction = interpreter.get_tensor(output_details[0]['index'])[0]

    # Get client data
    client, addr = server.accept()
    clientMessage = str(client.recv(30), 'utf-8')

    # Parse the recognition result
    trg_id, trg_class, trg_prob = parse_output(prediction, labels)

    # Display MPU6050 message
    print('MPU6050_Message:' + clientMessage)

    # Compare NANO and MPU6050
    if clientMessage == '1':
        case_message = 1
    if last_trg_class == labels[2]:
        case_curl = 1
    if case_curl == 1 and trg_class == labels[0]:
        finish_curl = 1
        case_curl = 0
    if case_message == 1 and finish_curl == 1:
        Curl_Count += 1
        case_message = 0
        finish_curl = 0
    if Curl_Count == key_in:
        print("finish curl")
        time.sleep(2)
        close_thread = 1
        break

    last_trg_class = trg_class
    vid.info = '{} ,Count:{} '.format(trg_class, Curl_Count)

    t_start = time.time()

vid.info = ('Finish %s times curls Please press Esc' % Curl_Count)

time.sleep(1)

print('-' * 30)
print(f'影像串流的線程是否已關閉 : {not vid.t.is_alive()}')
print('離開程式')
