from flask import Flask, jsonify, request
from ultralytics import YOLO
import requests
import cv2
import numpy as np
import os
from datetime import datetime

app = Flask(__name__)

# Konfigurasi
ESP32_CAM_URL = "http://192.168.1.101/capture"
ESP32_DETECTION_URL = "http://192.168.1.101/deteksi"  # URL endpoint ESP32 untuk menerima durasi
MODEL_PATH = "last.pt"
VEHICLE_CLASSES = [0]  # Sesuaikan dengan kelas kendaraan di model Anda
CONFIDENCE_THRESHOLD = 0.25
SNAPSHOT_DIR = "snapshots"
COOLDOWN_SEC = 3

# Inisialisasi
os.makedirs(SNAPSHOT_DIR, exist_ok=True)
model = YOLO(MODEL_PATH)
print(f"✅ Model YOLO dimuat dari {MODEL_PATH}")

def get_esp32_frame():
    """Mengambil frame dari ESP32-CAM"""
    try:
        response = requests.get(ESP32_CAM_URL, timeout=3)
        response.raise_for_status()
        img_array = np.frombuffer(response.content, np.uint8)
        return cv2.imdecode(img_array, cv2.IMREAD_COLOR)
    except Exception as e:
        print(f"⚠ Gagal ambil frame: {str(e)}")
        return None

def detect_vehicles(frame):
    """Deteksi kendaraan menggunakan YOLO"""
    if frame is None:
        return 0, None

    results = model(frame, conf=CONFIDENCE_THRESHOLD, verbose=False)[0]
    count = 0
    
    # Visualisasi bounding box
    for box in results.boxes:
        if int(box.cls[0]) in VEHICLE_CLASSES:
            count += 1
            x1, y1, x2, y2 = map(int, box.xyxy[0])
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
            cv2.putText(frame, f"{model.names[int(box.cls[0])]} {box.conf[0]:.2f}", 
                       (x1, y1-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0,255,0), 2)
    
    return count, frame

def determine_duration(vehicle_count):
    """Menentukan durasi berdasarkan jumlah kendaraan yang terdeteksi"""
    if vehicle_count == 1:
        return 5  # jarang
    elif vehicle_count == 2:
        return 10  # sedang
    elif vehicle_count >= 3:
        return 15  # padat

    elif vehicle_count == 0:
        return 0 # kosong (skip)
    else:
        return 30  # default

def send_duration_to_esp32(duration):
    """Mengirim durasi ke ESP32"""
    try:
        response = requests.post(ESP32_DETECTION_URL, json={'duration': duration}, timeout=3)
        response.raise_for_status()
        print(f"✅ Durasi {duration} detik berhasil dikirim ke ESP32")
        return True
    except Exception as e:
        print(f"⚠ Gagal mengirim durasi ke ESP32: {str(e)}")
        return False

@app.route('/trigger_capture', methods=['POST'])
def handle_capture():
    """Endpoint untuk trigger snapshot (hanya POST)"""
    try:
        # Validasi request
        if not request.is_json:
            return jsonify({
                "status": "error",
                "message": "Content-Type harus application/json"
            }), 415
            
        data = request.get_json()
        if not data or 'trigger' not in data or not data['trigger']:
            return jsonify({
                "status": "error",
                "message": "Request body harus {'trigger': true}"
            }), 400
        
        # Ambil frame
        frame = get_esp32_frame()
        if frame is None:
            return jsonify({
                "status": "error",
                "message": "Gagal mengambil frame dari ESP32"
            }), 500
        
        # Deteksi kendaraan
        count, detected_frame = detect_vehicles(frame)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = os.path.join(SNAPSHOT_DIR, f"capture_{timestamp}.jpg")
        
        # Simpan hasil
        cv2.imwrite(filename, detected_frame)
        
        # Tentukan durasi dan kirim ke ESP32
        duration = determine_duration(count)
        send_duration_to_esp32(duration)
        
        # Tampilkan hasil di terminal
        print(f"\n📸 [{timestamp}] Snapshot diambil")
        print(f"🚗 Kendaraan terdeteksi: {count}")
        print(f"⏱ Durasi lampu: {duration} detik")
        print(f"💾 Disimpan sebagai: {filename}")
        print("----------------------------------")
        
        return jsonify({
            "status": "success",
            "filename": filename,
            "vehicle_count": count,
            "duration": duration,
            "timestamp": timestamp
        })
        
    except Exception as e:
        print(f"❌ Error: {str(e)}")
        return jsonify({
            "status": "error",
            "message": "Internal server error"
        }), 500

@app.route('/trigger_capture', methods=['GET', 'PUT', 'DELETE', 'PATCH'])
def method_not_allowed():
    """Handle method yang tidak diizinkan"""
    return jsonify({
        "status": "error",
        "message": "Method not allowed. Gunakan POST"
    }), 405

@app.route('/status', methods=['GET'])
def get_status():
    """Endpoint untuk mengecek status server"""
    return jsonify({
        "status": "ready",
        "model": MODEL_PATH,
        "last_activity": datetime.now().isoformat()
    })

if __name__ == '__main__':
    print("🌐 Server Flask berjalan di http://0.0.0.0:5000")
    print("Endpoint tersedia:")
    print(f"- POST /trigger_capture : Trigger snapshot + deteksi")
    print(f"- GET /status           : Cek status server")
    
    app.run(host='0.0.0.0', port=5000)