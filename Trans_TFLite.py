import tensorflow as tf

# 載入您的 Keras 模型
print("正在載入 Keras 模型...")
model = tf.keras.models.load_model('keras_model.h5')
print("模型載入成功。")

# 顯示模型摘要
print("\n模型摘要:")
model.summary()

# 將模型轉換為 TensorFlow Lite 格式
print("\n正在將模型轉換為 TensorFlow Lite 格式...")
converter = tf.lite.TFLiteConverter.from_keras_model(model)

# 檢查輸入和輸出細節
print("\n輸入細節:")
for layer in model.layers:
    print(f"層: {layer.name}, 輸入形狀: {layer.input_shape}, 資料類型: {layer.dtype}")

print("\n輸出細節:")
for layer in model.layers:
    print(f"層: {layer.name}, 輸出形狀: {layer.output_shape}, 資料類型: {layer.dtype}")

# 進行模型轉換
try:
    tflite_model = converter.convert()
    print("模型轉換成功。")
except Exception as e:
    print("模型轉換過程中出現錯誤:", str(e))

# 將轉換後的模型保存為 .tflite 檔案
tflite_model_path = 'model2.tflite'
print(f"\n正在將轉換後的模型保存至 {tflite_model_path}...")
with open(tflite_model_path, 'wb') as f:
    f.write(tflite_model)
print("模型保存成功。")
