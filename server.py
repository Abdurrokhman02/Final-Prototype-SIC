from flask import Flask, jsonify, request
import requests
from ultralytics import YOLO
import time
import threading
import cv2
import numpy as np
import logging
from datetime import datetime

app = Flask(__name__)

# Konfigurasi logging untuk debug
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('traffic_debug.log'),
        logging.StreamHandler()
    ]
)

# Konfigurasi ESP32
ESP32_LIST = [
    {"id": "esp1", "ip": "192.168.1.101", "last_capture": None, "status": "unknown"},
    {"id": "esp2", "ip": "192.168.1.102", "last_capture": None, "status": "unknown"},
    {"id": "esp3", "ip": "192.168.1.103", "last_capture": None, "status": "unknown"}
]

# Model YOLO
model = YOLO("last.pt")
VEHICLE_CLASSES = [0]  # Sesuaikan dengan kelas kendaraan

# Variabel kontrol
current_esp_index = 0
system_active = True
debug_mode = True  # Set True untuk aktifkan debug

def log_debug(message):
    """Log pesan debug jika mode debug aktif"""
    if debug_mode:
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        logging.debug(f"[DEBUG] {timestamp} - {message}")

def get_frame(esp_ip):
    """Mengambil frame dari ESP32-CAM dengan logging"""
    try:
        url = f"http://{esp_ip}/capture"
        log_debug(f"Mencoba mengambil frame dari {url}")
        
        start_time = time.time()
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        
        img_array = np.frombuffer(response.content, np.uint8)
        frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)
        
        log_debug(f"Berhasil ambil frame dari {esp_ip} - Ukuran: {frame.shape if frame is not None else 'None'} - Waktu: {time.time()-start_time:.2f}s")
        return frame
        
    except Exception as e:
        logging.error(f"Gagal ambil frame dari {esp_ip}: {str(e)}")
        return None

def detect_vehicles(frame):
    """Deteksi kendaraan dengan logging detail"""
    if frame is None:
        return 0
    
    try:
        log_debug("Memulai deteksi YOLO...")
        start_time = time.time()
        
        results = model(frame, conf=0.25, verbose=debug_mode)[0]
        count = sum(1 for box in results.boxes if int(box.cls[0]) in VEHICLE_CLASSES)
        
        log_debug(f"Deteksi selesai - Kendaraan: {count} - Waktu: {time.time()-start_time:.2f}s")
        
        if debug_mode:
            # Simpan frame hasil deteksi untuk debugging
            debug_img = results.plot()
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            cv2.imwrite(f"debug/detection_{timestamp}.jpg", debug_img)
            
        return count
        
    except Exception as e:
        logging.error(f"Gagal deteksi kendaraan: {str(e)}")
        return 0

def set_light(esp_ip, command, duration):
    """Mengirim perintah ke ESP32 dengan logging"""
    try:
        url = f"http://{esp_ip}/set_lights"
        payload = {"command": command, "duration": duration}
        
        log_debug(f"Mengirim ke {url}: {payload}")
        
        start_time = time.time()
        response = requests.post(url, json=payload, timeout=3)
        response.raise_for_status()
        
        log_debug(f"Berhasil kirim ke {esp_ip} - Respon: {response.text} - Waktu: {time.time()-start_time:.2f}s")
        return True
        
    except Exception as e:
        logging.error(f"Gagal mengirim ke {esp_ip}: {str(e)}")
        return False

def traffic_control_loop():
    """Main control loop dengan logging ekstensif"""
    global current_esp_index
    
    while system_active:
        try:
            active_esp = ESP32_LIST[current_esp_index]
            next_esp = ESP32_LIST[(current_esp_index + 1) % 3]
            other_esp = ESP32_LIST[(current_esp_index + 2) % 3]
            
            logging.info(f"\n{'='*40}")
            logging.info(f"🔄 Memproses {active_esp['id']} ({active_esp['ip']})")
            
            # 1. Ambil gambar
            frame = get_frame(active_esp['ip'])
            active_esp['last_capture'] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            if frame is None:
                logging.warning("Frame kosong, melanjutkan ke ESP berikutnya...")
                current_esp_index = (current_esp_index + 1) % 3
                time.sleep(5)
                continue
                
            # 2. Deteksi kendaraan
            vehicle_count = detect_vehicles(frame)
            green_duration = max(5, vehicle_count * 3)  # Minimal 5 detik
            
            logging.info(f"📊 Hasil Deteksi: {vehicle_count} kendaraan -> Durasi Hijau: {green_duration}s")
            
            # 3. Kirim perintah lampu
            logging.info("🟢 Mengirim perintah lampu:")
            
            # ESP aktif: Hijau -> Kuning -> Merah
            if set_light(active_esp['ip'], "START_HIJAU", green_duration):
                active_esp['status'] = f"HIJAU ({green_duration}s)"
            
            # ESP berikutnya: Merah (selama hijau aktif + kuning)
            red_duration = green_duration + 3
            if set_light(next_esp['ip'], "START_MERAH", red_duration):
                next_esp['status'] = f"MERAH ({red_duration}s)"
            
            # ESP lainnya: Merah (durasi panjang)
            if set_light(other_esp['ip'], "START_MERAH", 30):
                other_esp['status'] = "MERAH (30s)"
            
            # 4. Tunggu durasi hijau + kuning selesai
            total_wait = green_duration + 3
            logging.info(f"⏳ Menunggu {total_wait} detik sebelum beralih...")
            
            for i in range(total_wait):
                if not system_active:
                    break
                time.sleep(1)
                
                # Log status setiap 5 detik
                if i % 5 == 0:
                    logging.debug(f"Menunggu... {i}/{total_wait} detik")
            
            # 5. Pindah ke ESP berikutnya
            current_esp_index = (current_esp_index + 1) % 3
            
        except Exception as e:
            logging.error(f"⚠ Kesalahan dalam control loop: {str(e)}")
            time.sleep(5)

@app.route('/start', methods=['POST', 'GET'])
def start_system():
    """Endpoint untuk memulai sistem"""
    global system_active
    system_active = True
    threading.Thread(target=traffic_control_loop, daemon=True).start()
    logging.info("Sistem dimulai")
    return jsonify({
        "status": "started",
        "debug_mode": debug_mode,
        "log_file": "traffic_debug.log"
    })

@app.route('/stop', methods=['POST', 'GET'])
def stop_system():
    """Endpoint untuk menghentikan sistem"""
    global system_active
    system_active = False
    logging.info("Sistem dihentikan")
    return jsonify({"status": "stopped"})

@app.route('/status', methods=['GET'])
def get_status():
    """Endpoint status sistem detail"""
    active_esp = ESP32_LIST[current_esp_index]
    return jsonify({
        "active": system_active,
        "current_esp": active_esp['id'],
        "all_esp": ESP32_LIST,
        "debug_mode": debug_mode,
        "timestamp": datetime.now().isoformat()
    })

@app.route('/test', methods=['GET'])
def test_connection():
    """Endpoint untuk test koneksi ke semua ESP"""
    results = []
    for esp in ESP32_LIST:
        try:
            start_time = time.time()
            response = requests.get(f"http://{esp['ip']}/status", timeout=3)
            ping_time = (time.time() - start_time) * 1000  # dalam ms
            results.append({
                "id": esp['id'],
                "ip": esp['ip'],
                "status": "online",
                "ping": f"{ping_time:.2f}ms",
                "response": response.json()
            })
        except Exception as e:
            results.append({
                "id": esp['id'],
                "ip": esp['ip'],
                "status": "offline",
                "error": str(e)
            })
    return jsonify({"test_results": results})

if __name__ == '__main__':
    # Buat folder debug jika tidak ada
    import os
    os.makedirs("debug", exist_ok=True)
    
    print("🌐 Sistem Traffic Light Controller (Debug Mode)")
    print("Endpoint:")
    print("- POST /start : Mulai sistem")
    print("- POST /stop  : Hentikan sistem")
    print("- GET /status : Status sistem")
    print("- GET /test   : Test koneksi ESP32")
    
    app.run(host='0.0.0.0', port=5000, debug=True)