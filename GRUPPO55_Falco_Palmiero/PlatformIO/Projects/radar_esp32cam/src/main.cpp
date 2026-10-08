#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ============ DA MODIFICARE ============
//const char* ssid = "TIM-79299292_5GHZ"; //2.4 GHz

const char* ssid = "TIM-79299292_2.4GHZ"; //2.4 GHz
const char* password = "GYX37h7ZsA52Zy7Q5FPxQN3x";
#define BOT_TOKEN "8668279408:AAGFMNeim1LO_j3NIqWS5Z_RCb4ilJUGPao"
#define CHAT_ID "803809627" // ID chat Telegram 
// =========================================
// Pin camera per modulo AI-Thinker

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN      4

// Variabili globali
camera_fb_t * fb = nullptr;
IPAddress telegramIP;
bool ipResolto = false;

void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
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
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 20;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init fallita con errore 0x%x\n", err);
  }
}

void inviaFotoTelegram() {
  if (!fb) {
    Serial.println("Errore acquisizione foto!");
    return;
  }

  Serial.printf("RAM libera prima dell'invio: %u bytes\n", ESP.getFreeHeap());
  Serial.println("Invio foto a Telegram...");

  unsigned long t0 = millis();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  bool connesso;
  if (ipResolto) {
    connesso = client.connect(telegramIP, 443);
  } else {
    connesso = client.connect("api.telegram.org", 443);
  }

  if (!connesso) {
    Serial.println("Connessione a Telegram fallita.");
    esp_camera_fb_return(fb);
    fb = nullptr;
    Serial.write('E');
    return;
  }

  client.setNoDelay(true);

  unsigned long t1 = millis();
  Serial.printf("Tempo connessione (DNS+TLS): %lu ms\n", t1 - t0);

  String boundary = "esp32camboundary";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  head += String(CHAT_ID) + "\r\n";
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"photo\"; filename=\"foto.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = head.length() + fb->len + tail.length();

  client.println("POST /bot" + String(BOT_TOKEN) + "/sendPhoto HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(totalLen));
  client.println("Connection: close");
  client.println();
  client.print(head);

  client.write(fb->buf, fb->len);

  client.print(tail);

  unsigned long t2 = millis();
  Serial.printf("Tempo invio dati: %lu ms\n", t2 - t1);

  Serial.println("--- Risposta di Telegram ---");
  unsigned long timeoutStart = millis();
  bool rispostaRicevuta = false;
  String rispostaCompleta = "";

  while ((client.connected() || client.available()) && (millis() - timeoutStart < 15000)) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
      rispostaCompleta += line;
      rispostaRicevuta = true;
      timeoutStart = millis();
    }
  }

  if (!rispostaRicevuta) {
    Serial.println("!!! Nessuna risposta ricevuta dal server entro il timeout !!!");
  }
  Serial.println("--- Fine risposta ---");

  client.stop();
  esp_camera_fb_return(fb);
  fb = nullptr;

  // ---- Invio ACK alla STM32 ----
  if (rispostaRicevuta && rispostaCompleta.indexOf("\"ok\":true") != -1) {
    Serial.write('K');
    Serial.println("ACK inviato: K (successo)");
  } else {
    Serial.write('E');
    Serial.println("ACK inviato: E (errore)");
  }
  // -------------------------------

  unsigned long t3 = millis();
  Serial.printf("Tempo totale invio+risposta: %lu ms\n", t3 - t1);
  Serial.println("Invio completato.");
}

void setup() {
  Serial.begin(115200);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  startCamera();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connessione WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connesso!");

  // ---- Segnale visivo di conferma WiFi ----
for (int i = 0; i < 3; i++) {
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(200);
  digitalWrite(FLASH_LED_PIN, LOW);
  delay(200);
}


  if (WiFi.hostByName("api.telegram.org", telegramIP)) {
    ipResolto = true;
    Serial.print("IP Telegram risolto: ");
    Serial.println(telegramIP);
  }
}

void loop() {
  if (Serial.available()) {
    char comando = Serial.read();
    if (comando == 'T') {
      digitalWrite(FLASH_LED_PIN, HIGH);

      // Scarta un frame "vecchio" rimasto in coda
      camera_fb_t * scarto = esp_camera_fb_get();
      if (scarto) esp_camera_fb_return(scarto);

      // Ora cattura il frame realmente aggiornato
      fb = esp_camera_fb_get();

      digitalWrite(FLASH_LED_PIN, LOW);

      if (fb) {
        inviaFotoTelegram();
      } else {
        Serial.println("Errore acquisizione foto!");
      }
    }
  }
  delay(10);
}