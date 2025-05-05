// ===== Library yang dibutuhkan =====
#include "esp_camera.h"              // Library untuk mengakses kamera ESP32-CAM
#include <WiFi.h>                    // Library untuk koneksi WiFi
#include <HTTPClient.h>             // Library untuk komunikasi HTTP ke server
#include <WebServer.h>              // Library untuk membuat web server lokal
#include <ArduinoJson.h>            // Library untuk parsing dan pembuatan data JSON
#include <Wire.h>                   // Library komunikasi I2C (untuk OLED)
#include <Adafruit_GFX.h>          // Library grafis Adafruit
#include <Adafruit_SSD1306.h>      // Library untuk OLED SSD1306

// ===== Konfigurasi model kamera =====
#define CAMERA_MODEL_WROVER_KIT     // Mendefinisikan jenis board kamera yang digunakan
#include "camera_pins.h"            // Pin bawaan kamera WROVER Kit

// ===== Konfigurasi OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 32                 // Pin SDA untuk OLED
#define OLED_SCL 33                 // Pin SCL untuk OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // Objek display OLED

// ===== Konfigurasi Jaringan =====
const char* ssid = "Kos ijo";       // SSID WiFi
const char* password = "Aslan199"; // Password WiFi
IPAddress local_IP(192, 168, 1, 103);   // IP statis untuk ESP32
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
const char* SERVER_URL = "http://192.168.1.3:5000"; // URL server Flask

// ===== Konfigurasi Pin LED =====
#define LED_MERAH 14
#define LED_KUNING 12
#define LED_HIJAU 13

// ===== Variabel kontrol lampu lalu lintas =====
enum LampuState { MERAH, KUNING_MENUJU_HIJAU, HIJAU, KUNING_MENUJU_MERAH };
LampuState currentState = MERAH;          // Status lampu saat ini
unsigned long stateStartTime = 0;         // Waktu mulai status lampu
unsigned long stateDuration = 0;          // Durasi status aktif
WebServer server(80);                     // Web server di port 80

// Durasi lampu hijau dan merah
unsigned long hijauDuration = 0;
unsigned long merahDuration = 0;

// ===== Deklarasi fungsi =====
void updateOLED();
void setLampuState(LampuState newState, unsigned long duration);
void updateLampuState();
void handleCapture();
void handleSetLights();
void handleStatus();
String getStateName(LampuState state);
void sendToServer(String payload);
void resetLEDs();

// ===== Fungsi Setup (dijalankan sekali saat boot) =====
void setup() {
  Serial.begin(115200);  // Mulai serial monitor

  // Inisialisasi OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED allocation failed");
    while(1);
  }
  display.clearDisplay();
  display.display();

  // Inisialisasi pin LED
  pinMode(LED_MERAH, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_HIJAU, OUTPUT);
  resetLEDs();                        // Matikan semua LED awalnya
  setLampuState(MERAH, 0);            // Set awal ke MERAH

  // Koneksi ke WiFi
  WiFi.config(local_IP, gateway, subnet); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");

  // Inisialisasi kamera ESP32-CAM
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; // Data pin kamera
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_SVGA;     // Ukuran frame SVGA (800x600)
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;              // Kualitas JPEG (semakin kecil, semakin bagus)
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x", err);
    while(1) delay(100);
  }

  // Register HTTP endpoint
  server.on("/capture", HTTP_GET, handleCapture);     // Ambil gambar dari kamera
  server.on("/set_lights", HTTP_POST, handleSetLights); // Ubah status lampu
  server.on("/status", HTTP_GET, handleStatus);       // Status saat ini
  server.begin();

  // Kirim status awal ke server
  String payload = "{\"esp_id\":\"esp3\",\"status\":\"ready\"}";
  sendToServer(payload);
}

// ===== Fungsi loop utama =====
void loop() {
  server.handleClient();    // Tangani request HTTP
  updateLampuState();       // Periksa apakah perlu ganti status lampu
  updateOLED();             // Perbarui tampilan OLED
  delay(100);
}

// ===== Fungsi untuk memperbarui OLED =====
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setCursor(0, 0);

  // Tampilkan status lampu
  switch(currentState) {
    case MERAH: display.println("MERAH"); break;
    case KUNING_MENUJU_HIJAU:
    case KUNING_MENUJU_MERAH: display.println("KUNING"); break;
    case HIJAU: display.println("HIJAU"); break;
  }

  // Tampilkan hitung mundur
  if (stateDuration > 0) {
    unsigned long remaining = (stateDuration - (millis() - stateStartTime)) / 1000;
    if (remaining > 0) {
      display.setTextSize(4);
      display.setCursor(30, 30);
      display.println(String(remaining) + "s");
    }
  }

  display.display();
}

// ===== Fungsi untuk mematikan semua LED =====
void resetLEDs() {
  digitalWrite(LED_MERAH, LOW);
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_HIJAU, LOW);
}

// ===== Fungsi untuk mengatur status lampu =====
void setLampuState(LampuState newState, unsigned long duration) {
  Serial.printf("Changing state from %s to %s with duration %lu ms\n", 
                 getStateName(currentState).c_str(), 
                 getStateName(newState).c_str(),
                 duration);

  resetLEDs();                     // Matikan semua LED
  currentState = newState;
  stateStartTime = millis();      // Catat waktu mulai status ini
  stateDuration = duration;       // Atur durasi status

  // Nyalakan LED sesuai status
  switch(currentState) {
    case MERAH: digitalWrite(LED_MERAH, HIGH); break;
    case KUNING_MENUJU_HIJAU:
    case KUNING_MENUJU_MERAH: digitalWrite(LED_KUNING, HIGH); break;
    case HIJAU: digitalWrite(LED_HIJAU, HIGH); break;
  }
}

// ===== Fungsi untuk berpindah status secara otomatis =====
void updateLampuState() {
  if (stateDuration == 0) return;

  unsigned long elapsed = millis() - stateStartTime;
  if (elapsed >= stateDuration) {
    switch(currentState) {
      case MERAH:
        setLampuState(KUNING_MENUJU_HIJAU, 3000);
        break;
      case KUNING_MENUJU_HIJAU:
        setLampuState(HIJAU, hijauDuration);
        break;
      case HIJAU:
        setLampuState(KUNING_MENUJU_MERAH, 3000);
        break;
      case KUNING_MENUJU_MERAH:
        setLampuState(MERAH, merahDuration);
        break;
    }
  }
}

// ===== Endpoint: Mengambil gambar kamera =====
void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera error");
    return;
  }
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ===== Endpoint: Mengatur status lampu dari HTTP POST =====
void handleSetLights() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad request");
    return;
  }

  // Ambil perintah dan durasi dari JSON
  String command = doc["command"];
  unsigned long duration = doc["duration"];

  // Proses perintah dari server
  if (command == "START_HIJAU") {
    hijauDuration = duration * 1000;
    setLampuState(KUNING_MENUJU_HIJAU, 3000);
  } else if (command == "START_MERAH") {
    merahDuration = duration * 1000;
    setLampuState(MERAH, merahDuration);
  } else if (command == "FORCE_MERAH") {
    setLampuState(MERAH, 0);
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ===== Endpoint: Mengembalikan status lampu saat ini =====
void handleStatus() {
  DynamicJsonDocument doc(256);
  doc["state"] = getStateName(currentState);
  doc["remaining"] = (stateDuration > 0) ? (stateDuration - (millis() - stateStartTime)) / 1000 : 0;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ===== Helper: Mendapatkan nama status dari enum =====
String getStateName(LampuState state) {
  switch(state) {
    case MERAH: return "MERAH";
    case KUNING_MENUJU_HIJAU: return "KUNING_MENUJU_HIJAU";
    case HIJAU: return "HIJAU";
    case KUNING_MENUJU_MERAH: return "KUNING_MENUJU_MERAH";
    default: return "UNKNOWN";
  }
}

// ===== Fungsi untuk mengirim JSON payload ke server Flask =====
void sendToServer(String payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SERVER] Not connected to WiFi");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);  // Kirim data
  if (httpCode > 0) {
    Serial.printf("[SERVER] Response: %d\n", httpCode);
  } else {
    Serial.printf("[SERVER] Failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}
