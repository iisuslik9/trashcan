#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <ArduinoJson.h>

// === ПИНЫ ===
#define DHTPIN 16       // D0 = GPIO16
#define DHTTYPE DHT11
#define PHOTO_PIN A0     // 
#define LED1_PIN 14      // D5 = GPIO14
#define LED2_PIN 12      // D6 = GPIO12  
#define LED3_PIN 13      // D7 = GPIO13
#define RGB_R_PIN 5      // D1 = GPIO5 (RX)
#define RGB_G_PIN 4      // D2 = GPIO4 (SD2)
#define RGB_B_PIN 0      // D3 = GPIO0 (RX)
#define BUZZER_PIN 2     // D4 = GPIO2 (SD3)
#define RELAY_PIN 15     // D8 = GPIO15

WiFiClientSecure client;
WiFiClient wifiClient;  // Для HTTP тестов



// === SUPABASE ===
const char* SUPABASE_URL = "https://yndjuqvejwgxostadikf.supabase.co";
const char* SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InluZGp1cXZlandneG9zdGFkaWtmIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjU2MDQzMzAsImV4cCI6MjA4MTE4MDMzMH0.qRzRvFjnKtpoWIOEhsGWsdqgfz0CexO7cZPxZZP6Tus";

DHT dht(DHTPIN, DHTTYPE);
WiFiManager wifiManager;





// Текущие значения управления
int led1_val = 0, led2_val = 0, led3_val = 0;
int rgb_r = 0, rgb_g = 0, rgb_b = 0;
bool strip_state = false, buzzer_state = false;
bool manualOff = false;  // true = лента выключена вручную
bool buzzerTriggered = false; 

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
  //Serial.begin(115200);
  Serial.begin(74880);
  
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
  //digitalWrite(BUZZER_PIN, LOW);
  
  dht.begin();
  
  // WiFi Manager (создаёт точку доступа для настройки)
  wifiManager.autoConnect("SmartHome_NodeMCU");
  client.setInsecure();  
  
  Serial.println(" NodeMCU подключен! IP: " + WiFi.localIP().toString());
  Serial.println(" Отправка данных каждую секунду");
  Serial.println(" Автоматика ленты по фоторезистору");
}

void loop() {

static unsigned long lastTest = 0;

  // Читаем датчики
  // ПРОВЕРКА DHT на NaN
  float temp_dht = dht.readTemperature();
  float hum_dht = dht.readHumidity();
  Serial.printf("📡 T:%.1f H:%.1f\n", 
    temp_dht, hum_dht);
  // Фильтр NaN
  if (isnan(temp_dht) || isnan(hum_dht)) {
    Serial.println("❌ DHT ошибка - пропускаем");
    temp_dht = 25.0;  // дефолт
    hum_dht = 50.0;
  }
  
  int light = analogRead(PHOTO_PIN); // 0-1023 (0=темно, 1023=светло)
   Serial.printf("📡 T:%.1f H:%.1f L:%d | Лента:%s | Ручное:%s\n", 
    temp_dht, hum_dht, light, strip_state?"ВКЛ":"ВЫКЛ", manualOff?"ДА":"НЕТ");
  Serial.printf("T:%.1f°C H:%.1f%% L:%d\n", temp_dht, hum_dht, light);
  //if (isnan(temp_dht) || temp_dht > 50 || temp_dht < -10) temp_dht = 25.0;
  if (isnan(hum_dht) || hum_dht > 100 || hum_dht < 0) hum_dht = 50.0;
  if (light < 0 || light > 1023) light = 512;

  
  Serial.printf(" %.1f°C |  %.1f%% |  %d | Лента: %s\n", 
                temp_dht, hum_dht, light, strip_state ? "ВКЛ" : "ВЫКЛ");

 // === ПРОВЕРКА ТАЙМЕРА ===
  unsigned long timerDuration = (timer_hours * 3600UL + timer_minutes * 60UL) * 1000UL;
  if (timerActive && (millis() - timerStart >= timerDuration)) {
    timerActive = false;
    if (!manualOff) strip_state = false;
    Serial.println("⏰ Таймер истёк → ЛЕНТА ВЫКЛ");
  }

  // === АВТОМАТИКА ЛЕНТЫ ПО ФОТОРЕЗИСТОРУ ===


  // ✅ АВТОМАТИКА (только если НЕ ручное управление)
  if (!manualOff) {
    static unsigned long lastAutoChange = 0;
    if (millis() - lastAutoChange > MIN_DURATION_MS) {
      bool isDark = (light < LIGHT_THRESHOLD);
      if (isDark && !strip_state) {
        strip_state = true; lastAutoChange = millis();
        Serial.println("🌙 АВТО: ВКЛ (темно)");
      } else if (!isDark && strip_state && !timerActive) {
        strip_state = false; lastAutoChange = millis();
        Serial.println("☀️ АВТО: ВЫКЛ (светло)");
      }
    }
  }

  // === ПРИМЕНЯЕМ УПРАВЛЕНИЕ ===
  analogWrite(LED1_PIN, led1_val);
  analogWrite(LED2_PIN, led2_val);
  analogWrite(LED3_PIN, led3_val);
  analogWrite(RGB_R_PIN, rgb_r);
  analogWrite(RGB_G_PIN, rgb_g);
  analogWrite(RGB_B_PIN, rgb_b);
  
  digitalWrite(RELAY_PIN, strip_state ? HIGH : LOW); // HIGH = реле ВКЛ

  if (buzzer_state && !buzzerTriggered) {
    playBeep();
    buzzerTriggered = true;
    Serial.println("🎵 Играем beep!");
  }
  static bool last_buzzer_cmd = false;
    if (buzzer_state && !last_buzzer_cmd) {
    playBeep(); 
    Serial.println("🎵 Играем beep!");
  }
  last_buzzer_cmd = buzzer_state;
  digitalWrite(BUZZER_PIN, LOW);

  //if (!buzzer_state) buzzerTriggered = false;  // Сброс при выключении

  
  //lastBuzzerState = buzzer_state;

  // Отправляем данные в Supabase
  sendSensorData(temp_dht, hum_dht, light);
  
  // Читаем команды из Supabase
  loadControls();
  


  delay(1000); // Цикл каждую 1 секунду
}

void sendSensorData(float temp_dht, float hum_dht, int light) {
  if (WiFi.status() == WL_CONNECTED) {
    //WiFiClientSecure client;
    //client.setInsecure();
    HTTPClient http;
    http.begin(client, String(SUPABASE_URL) + "/rest/v1/sensor_data");
    http.addHeader("apikey", SUPABASE_KEY);
    //http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Prefer", "return=minimal");
    
    DynamicJsonDocument doc(1024);
    doc["temperature"] = (double)temp_dht;     
    doc["humidity"] = (double)hum_dht;         
    doc["light"] = light; 
    /*doc["led1"] = (int)led1_val;
    doc["led2"] = (int)led2_val;
    doc["led3"] = (int)led3_val;
    doc["rgb_r"] = (int)rgb_r;
    doc["rgb_g"] = (int)rgb_g;
    doc["rgb_b"] = (int)rgb_b;*/
    doc["strip"] = strip_state;
    //doc["buzzer"] = buzzer_state;
    doc["timer_h"] = timer_hours;
    doc["timer_m"] = timer_minutes;
    //doc["timer_active"] = timerActive;
    doc["timer_active"] = timerActive;
    doc["manual_off"] = manualOff;

    
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
    
    //http.begin(client, String(SUPABASE_URL) + "/rest/v1/controls?eq=id.eq.1");
    //http.begin(client, String(SUPABASE_URL) + "/rest/v1/controls?eq=id=1");
    http.begin(client, String(SUPABASE_URL) + "/rest/v1/controls?select=*");

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
        timer_minutes = doc[0]["timer_minutes"] | 0;
    
        // === Ручной ТАЙМЕР ===
        if (newStrip && !strip_state ) {  
          timerActive = true;
          timerStart = millis();
          manualOff = false;
          strip_state = true;
          Serial.printf("⏳ Ручное ВКЛ → Таймер %d:%02d\n", timer_hours, timer_minutes);
        } 
        else if (!newStrip && strip_state ) {  
          manualOff = true;
          timerActive = false;  //Остановить таймер
          strip_state = false;
          Serial.println("🖐️ Ручное выкл (автоматика заблокирована)");
        }


        Serial.printf("LED:%d,%d,%d | RGB:%d,%d,%d | Таймер:%d:%02d\n", 
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

void testInternet() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(wifiClient, "http://httpbin.org/ip");  // ✅ WiFiClient + URL
    int code = http.GET();
    Serial.printf("🌐 Internet: %d\n", code);
    http.end();
  } else {
    Serial.println("❌ WiFi отключён");
  }
}

