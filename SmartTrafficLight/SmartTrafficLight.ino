#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#define CAMERA_MODEL_WROVER_KIT
#include "camera_pins.h"

// ========== KONFIGURASI JARINGAN ==========
const char* ssid = "Kos ijo";
const char* password = "Aslan199";

// IP Statis ESP32 (cam3)
IPAddress local_IP(192, 168, 1, 103);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// IP Server Flask
const char* SERVER_URL = "http://192.168.1.3:5000";

// ========== KONFIGURASI HARDWARE ==========
#define LED_MERAH 12
#define LED_KUNING 14  // Pin 4 diubah ke 14 untuk AI Thinker
#define LED_HIJAU 13

// ========== VARIABEL KONTROL ==========
enum LampuState { MERAH, KUNING_MENUJU_HIJAU, HIJAU, KUNING_MENUJU_MERAH };
LampuState currentState = MERAH;
unsigned long stateStartTime = 0;
unsigned long stateDuration = 0;
WebServer server(80);

// ========== PROTOTIPE FUNGSI ==========
String getStateName(LampuState state);
void sendToServer(String payload);

void setup() {
  Serial.begin(115200);
  
  // Inisialisasi LED
  pinMode(LED_MERAH, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_HIJAU, OUTPUT);
  setLampuState(MERAH, 0);

  // Koneksi WiFi
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Inisialisasi Kamera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
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
  config.frame_size = FRAMESIZE_SVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x", err);
    while(1) delay(100);
  }

  // Endpoint Server
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/set_lights", HTTP_POST, handleSetLights);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  // Lapor ke server utama
  String payload = "{\"esp_id\":\"esp3\",\"status\":\"ready\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  sendToServer(payload);
}

void loop() {
  server.handleClient();
  updateLampuState();
}

// ========== FUNGSI UTAMA ==========
void setLampuState(LampuState newState, unsigned long duration) {
  currentState = newState;
  stateStartTime = millis();
  stateDuration = duration;

  // Update LED
  digitalWrite(LED_MERAH, currentState == MERAH || currentState == KUNING_MENUJU_MERAH);
  digitalWrite(LED_KUNING, currentState == KUNING_MENUJU_HIJAU || currentState == KUNING_MENUJU_MERAH);
  digitalWrite(LED_HIJAU, currentState == HIJAU);

  // Debug log
  Serial.println("State changed to: " + getStateName(currentState) + " for " + String(duration/1000) + "s");
  
  // Kirim update ke server
  String payload = "{\"esp_id\":\"esp3\",\"state\":\"" + getStateName(currentState) + "\"";
  payload += ",\"remaining\":" + String(duration/1000) + "}";
  sendToServer(payload);
}

void updateLampuState() {
  if (stateDuration == 0) return;
  
  unsigned long elapsed = millis() - stateStartTime;
  if (elapsed >= stateDuration) {
    switch(currentState) {
      case KUNING_MENUJU_HIJAU:
        setLampuState(HIJAU, stateDuration);
        break;
      case HIJAU:
        setLampuState(KUNING_MENUJU_MERAH, 3000);
        break;
      case KUNING_MENUJU_MERAH:
        setLampuState(MERAH, 0);
        break;
      default: break;
    }
  }
}

// ========== HANDLER HTTP ==========
void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera error");
    return;
  }
  
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

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

  String command = doc["command"];
  unsigned long duration = doc["duration"];

  if (command == "START_HIJAU") {
    setLampuState(KUNING_MENUJU_HIJAU, 3000);
    stateDuration = duration * 1000;
  } 
  else if (command == "START_MERAH") {
    setLampuState(MERAH, duration * 1000);
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleStatus() {
  DynamicJsonDocument doc(256);
  doc["state"] = getStateName(currentState);
  doc["remaining"] = (stateDuration > 0) ? (stateDuration - (millis() - stateStartTime)) / 1000 : 0;
  doc["ip"] = WiFi.localIP().toString();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== FUNGSI PENDUKUNG ==========
String getStateName(LampuState state) {
  switch(state) {
    case MERAH: return "MERAH";
    case KUNING_MENUJU_HIJAU: return "KUNING_MENUJU_HIJAU";
    case HIJAU: return "HIJAU";
    case KUNING_MENUJU_MERAH: return "KUNING_MENUJU_MERAH";
    default: return "UNKNOWN";
  }
}

void sendToServer(String payload) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.printf("[SERVER] Response: %d\n", httpCode);
  } else {
    Serial.printf("[SERVER] Failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}