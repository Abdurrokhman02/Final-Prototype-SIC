#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// Model Kamera
#define CAMERA_MODEL_WROVER_KIT
#include "camera_pins.h"

// Konfigurasi OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 32
#define OLED_SCL 33
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Konfigurasi WiFi
const char* ssid = "Kos ijo";
const char* password = "Aslan199";

// Konfigurasi IP Statis
IPAddress local_IP(192, 168, 1, 101);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

const char* flaskTriggerEndpoint = "http://192.168.1.3:5000/trigger_capture";

// Pin LED
#define LED_MERAH 14
#define LED_KUNING 12
#define LED_HIJAU 13

// Durasi Lampu (ms)
#define DURASI_MERAH 10000
#define DURASI_KUNING 3000
unsigned long DURASI_HIJAU = 10000; // Default value, will be updated from server

void startCameraServer();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Inisialisasi I2C dan OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  // Inisialisasi LED
  pinMode(LED_MERAH, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_HIJAU, OUTPUT);
  digitalWrite(LED_MERAH, LOW);
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_HIJAU, LOW);

  // Tampilkan startup screen
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Starting...");
  display.display();
  delay(2000);

  // Konfigurasi kamera
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
  
  // Inisialisasi kamera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Camera Error");
    display.display();
    while(1) delay(100);
  }

  // Konfigurasi WiFi
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Failed to configure static IP");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting to");
  display.println("WiFi...");
  display.display();
  
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect!");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi Failed!");
    display.display();
    while(1) {
      digitalWrite(LED_MERAH, HIGH);
      delay(500);
      digitalWrite(LED_MERAH, LOW);
      delay(500);
    }
  }
  
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  delay(3000);
  startCameraServer();
  Serial.println("Camera streaming server started");
}

void triggerSnapshot() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  HTTPClient http;
  http.begin(flaskTriggerEndpoint);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST("{\"trigger\":true}");
  
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Server response: " + payload);
      
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        http.end();
        return;
      }
      
      if (doc.containsKey("duration")) {
        // Konversi eksplisit ke unsigned long
        unsigned long durationSec = doc["duration"];
        DURASI_HIJAU = durationSec * 1000;
        
        Serial.printf("Got new green duration: %lu ms\n", DURASI_HIJAU);
      } else {
        Serial.println("No duration field in response");
      }
    }
  } else {
    Serial.printf("[HTTP] Failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}

void updateOLED(String state, unsigned long remainingTime) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0,0);
  display.println(state);
  
  display.setCursor(0, 30);
  display.print(remainingTime / 1000);
  display.println("s");
  
  if (state == "HIJAU") {
    display.setCursor(0, 50);
    display.print(DURASI_HIJAU / 1000);
    display.println("s");
  }
  
  display.display();
}

void loop() {
  static enum { MERAH, KUNING_MENUJU_HIJAU, HIJAU, KUNING_MENUJU_MERAH } state = MERAH;
  static unsigned long lastChange = millis();
  static bool shouldTrigger = false;

  // Kontrol LED
  digitalWrite(LED_MERAH, state == MERAH || state == KUNING_MENUJU_MERAH);
  digitalWrite(LED_KUNING, state == KUNING_MENUJU_HIJAU || state == KUNING_MENUJU_MERAH);
  digitalWrite(LED_HIJAU, state == HIJAU);

  // State machine
  unsigned long now = millis();
  unsigned long elapsed = now - lastChange;
  unsigned long remainingTime = 0;
  String stateName = "";
  
  switch(state) {
    case MERAH:
      remainingTime = DURASI_MERAH - elapsed;
      stateName = "MERAH";
      if (elapsed > DURASI_MERAH) {
        state = KUNING_MENUJU_HIJAU;
        lastChange = now;
        shouldTrigger = true;
      }
      break;
      
    case KUNING_MENUJU_HIJAU:
      remainingTime = DURASI_KUNING - elapsed;
      stateName = "KUNING";
      if (elapsed > DURASI_KUNING) {
        state = HIJAU;
        lastChange = now;
      } else if (shouldTrigger && (elapsed > 1000)) {
        triggerSnapshot();
        shouldTrigger = false;
      }
      break;
      
    case HIJAU:
      remainingTime = DURASI_HIJAU - elapsed;
      stateName = "HIJAU";
      if (elapsed > DURASI_HIJAU) {
        state = KUNING_MENUJU_MERAH;
        lastChange = now;
        shouldTrigger = true;
      }
      break;
      
    case KUNING_MENUJU_MERAH:
      remainingTime = DURASI_KUNING - elapsed;
      stateName = "KUNING";
      if (elapsed > DURASI_KUNING) {
        state = MERAH;
        lastChange = now;
      } else if (shouldTrigger && (elapsed > 1000)) {
        triggerSnapshot();
        shouldTrigger = false;
      }
      break;
  }

  // Update OLED
  static unsigned long lastOLEDUpdate = 0;
  if (now - lastOLEDUpdate > 200) {
    updateOLED(stateName, remainingTime);
    lastOLEDUpdate = now;
  }

  delay(50);
}