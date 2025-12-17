#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <ArduinoJson.h>

// === ПИНЫ ===
#define DHT_PIN D0        // DHT11 Data
#define DHT_TYPE DHT11
#define PHOTO_PIN A0      // Фоторезистор
#define LED1_PIN D5       // LED1
#define LED2_PIN D6       // LED2
#define LED3_PIN D7       // LED3
#define RGB_R_PIN D1      // RGB R
#define RGB_G_PIN D2      // RGB G
#define RGB_B_PIN D3      // RGB B
#define BUZZER_PIN D4     // Активный зуммер
#define RELAY_PIN D8      // Реле (лента)

// === SUPABASE ===
const char* SUPABASE_URL = "https://yndjuqvejwgxostadikf.supabase.co";
const char* SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InluZGp1cXZlandneG9zdGFkaWtmIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjU2MDQzMzAsImV4cCI6MjA4MTE4MDMzMH0.qRzRvFjnKtpoWIOEhsGWsdqgfz0CexO7cZPxZZP6Tus";

DHT dht(DHT_PIN, DHT_TYPE);
WiFiManager wifiManager;

// Текущие значения управления
int led1_val = 0, led2_val = 0, led3_val = 0;
int rgb_r = 0, rgb_g = 0, rgb_b = 0;
bool strip_state = false, buzzer_state = false;
bool manualOff = false;  // true = лента выключена вручную

// Автоматика по фоторезистору
const int LIGHT_THRESHOLD = 300;  // порог темноты 
const int MIN_DURATION_MS = 10000; // мин. время между авто-включениями (10 сек)

// Таймер ручного включения
unsigned long timerStart = 0;
int timer_hours = 0, timer_minutes = 30;  // Настраиваемое время таймера
bool timerActive = false;

bool lastBuzzerState = false;
void playBeep() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(200);
    digitalWrite(BUZZER_PIN, LOW);  delay(150);
  }
}

void setup() {
  Serial.begin(115200);
  
  // Инициализация пинов
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  
  analogWriteRange(255); // PWM 0-255
  digitalWrite(BUZZER_PIN, LOW);
  
  dht.begin();
  
  // WiFi Manager (создаёт точку доступа для настройки)
  wifiManager.autoConnect("SmartHome_NodeMCU");
  
  Serial.println(" NodeMCU подключен! IP: " + WiFi.localIP().toString());
  Serial.println(" Отправка данных каждую секунду");
  Serial.println(" Автоматика ленты по фоторезистору");
}

void loop() {
  // Читаем датчики
  float temp_dht = dht.readTemperature();
  float hum_dht = dht.readHumidity();
  int light = analogRead(PHOTO_PIN); // 0-1023 (0=темно, 1023=светло)
  
  Serial.printf(" %.1f°C |  %.1f%% |  %d | Лента: %s\n", 
                temp_dht, hum_dht, light, strip_state ? "ВКЛ" : "ВЫКЛ");

 // === ПРОВЕРКА ТАЙМЕРА ===
  unsigned long timerDuration = (timer_hours * 3600UL + timer_minutes * 60UL) * 1000UL;
  if (timerActive && (millis() - timerStart >= timerDuration)) {
    timerActive = false;
    strip_state = false;
    Serial.println("Таймер истёк → ЛЕНТА ВЫКЛ");
  }

  // === АВТОМАТИКА ЛЕНТЫ ПО ФОТОРЕЗИСТОРУ ===
  static unsigned long lastAutoChange = 0;
  static bool wasDark = false;
  
  if (millis() - lastAutoChange > MIN_DURATION_MS) {
    bool isDark = (light < LIGHT_THRESHOLD);
    

    if (isDark && !strip_state && !manualOff) {
      strip_state = true;
      lastAutoChange = millis();
      Serial.println("🌙 АВТО: включаем ленту (темно)");
    }

    else if (!isDark && strip_state) {
      strip_state = false;
      lastAutoChange = millis();
      Serial.println("☀️ АВТО: выключаем ленту (светло)");
    }
    
    wasDark = isDark;
  }

  // === ПРИМЕНЯЕМ УПРАВЛЕНИЕ ===
  analogWrite(LED1_PIN, led1_val);
  analogWrite(LED2_PIN, led2_val);
  analogWrite(LED3_PIN, led3_val);
  analogWrite(RGB_R_PIN, rgb_r);
  analogWrite(RGB_G_PIN, rgb_g);
  analogWrite(RGB_B_PIN, rgb_b);
  digitalWrite(BUZZER_PIN, buzzer_state ? HIGH : LOW);
  digitalWrite(RELAY_PIN, strip_state ? HIGH : LOW); // HIGH = реле ВКЛ

  if (buzzer_state && !lastBuzzerState) {
    Serial.println("🎵 Играем beep на зуммере!");
    playBeep();  // мелодия ТОЛЬКО при включении кнопки
  }
  digitalWrite(BUZZER_PIN, LOW);  // всегда выключен после мелодии
  lastBuzzerState = buzzer_state;
  
  // Отправляем данные в Supabase
  sendSensorData(temp_dht, hum_dht, light);
  
  // Читаем команды из Supabase
  loadControls();
  
  delay(1000); // Цикл каждую секунду
}

void sendSensorData(float temp_dht, float hum_dht, int light) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(SUPABASE_URL) + "/rest/v1/sensor_data");
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(512);
    doc["temperature"] = temp_dht;     
    doc["humidity"] = hum_dht;         
    doc["light"] = light; 
    doc["led1_brightness"] = led1_val;
    doc["led2_brightness"] = led2_val;
    doc["led3_brightness"] = led3_val;
    doc["rgb_r"] = rgb_r;
    doc["rgb_g"] = rgb_g;
    doc["rgb_b"] = rgb_b;
    doc["strip_state"] = strip_state;
    doc["buzzer"] = buzzer_state;
    doc["timer_h"] = timer_hours;
    doc["timer_m"] = timer_minutes;
    doc["timer_active"] = timerActive;
    
    String json;
    serializeJson(doc, json);
    
    int code = http.POST(json);
    if (code == 201) {
      Serial.println("Данные отправлены в Supabase");
    } else {
      Serial.println("❌ Ошибка Supabase: " + String(code));
    }
    http.end();
  }
}

void loadControls() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(SUPABASE_URL) + "/rest/v1/controls");
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    
    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(512);
      deserializeJson(doc, payload);
      
      if (doc.size() > 0) {
        led1_val = doc[0]["led1"] | 0;
        led2_val = doc[0]["led2"] | 0;
        led3_val = doc[0]["led3"] | 0;
        rgb_r = doc[0]["rgb_r"] | 0;
        rgb_g = doc[0]["rgb_g"] | 0;
        rgb_b = doc[0]["rgb_b"] | 0;
        bool newStrip = doc[0]["strip"] | false;
        buzzer_state = doc[0]["buzzer"] | false;
        timer_hours = doc[0]["timer_hours"] | 0;
        timer_minutes = doc[0]["timer_minutes"] | 30;
    

        // === Ручной ТАЙМЕР ===
        if (newStrip && !strip_state) {  // ВКЛ в интерфейсе
          timerActive = true;
          timerStart = millis();
          manualOff = false;
          strip_state = true;
           Serial.printf("⏳ Ручное ВКЛ → Таймер %d:%02d\n", timer_hours, timer_minutes);
        } 
        else if (!newStrip && strip_state && !timerActive) {
          manualOff = true;
          strip_state = false;
          Serial.println("🖐️ Ручное выключение (заблокирована автоматика)");
        } else {
          strip_state = newStrip;
        }


        Serial.printf("🎛️ LED:%d,%d,%d | RGB:%d,%d,%d | Таймер:%d:%02d\n", 
                      led1_val, led2_val, led3_val, rgb_r, rgb_g, rgb_b, 
                      timer_hours, timer_minutes);
        Serial.print("Лента: "); Serial.print(strip_state ? "ВКЛ" : "ВЫКЛ");


      }
    } else {
      Serial.println("❌ Ошибка чтения controls: " + String(code));
    }
    http.end();
  }
}

