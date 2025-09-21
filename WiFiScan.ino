#include <ESP8266WiFi.h> //library untuk wifi pada esp8266
#include <ESP8266WebServer.h> //library untuk membuat website html
#include <AntaresESPMQTT.h> //library antares untuk mqtt pubsliher
#include "DHT.h" //library untuk sensor DHT

// ===================== KONFIGURASI =====================
#define ACCESSKEY     "561920a4f7503032:07ad51163677e0c5" //menghubungkan esp ke antares
#define PROJECT_NAME  "armeilia0" //nama project yang ada di antares
#define DEVICE_NAME   "miaw" //nama device yang ada di antares

const char* ssid     = "m";          // Ganti dengan SSID WiFi
const char* password = "12345678";   // Password WiFi

#define DHTPIN        D4             // Pin DHT11
#define DHTTYPE       DHT11
#define PIR_PIN       D5             // Pin PIR sensor
#define LED_PIN       D1             // Pin LED eksternal

const unsigned long PUBLISH_INTERVAL = 5000; // kirim ke Antares setiap 5 detik

// ===================== OBJEK ===========================
AntaresESPMQTT antares(ACCESSKEY); //untuk berkomunikasi dengan platform antares (cloud IoT Indonesia)
ESP8266WebServer server(80); //port HTTP standar (default web browser)
DHT dht(DHTPIN, DHTTYPE); //membuat objek bernama dht untuk berkomunikasi dengan sensor dht11

unsigned long lastPublish = 0; //menyimpan waktu terakhir ketika data terakhir kali dipublikasikan atau dikirim
int motion_state = LOW; //status awal kondisi pir (low=mati, deteksi gerakan high = hidup)
float temperature = 0; //nilai awal sensor suhu
float humidity = 0; //nilai awal sensor kelembapan
bool ledState = LOW; //status awal kondisi lampu

// ===================== FUNGSI ==========================
String htmlPage() { //untuk tampilan website (halaman utama)
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<title>ESP8266 Status</title>";
  page += "<style>";
  page += "body{font-family:Helvetica;text-align:center;margin-top:40px;}";
  page += ".data{font-size:20px;margin:10px;}";
  page += ".on{color:green;font-weight:bold;} .off{color:red;font-weight:bold;}";
  page += "</style></head><body>";
  page += "<h1>NodeMCU Status</h1>";
  page += "<p class='data'>WiFi: <b>" + String(ssid) + "</b></p>";
  page += "<p class='data'>IP Address: " + WiFi.localIP().toString() + "</p>";
  page += "<p class='data'>Koneksi: <span id='wifi'></span></p>";
  page += "<p class='data'>Suhu: <span id='temp'></span> °C</p>";
  page += "<p class='data'>Kelembapan: <span id='hum'></span> %</p>";
  page += "<p class='data'>PIR: <span id='pir'></span></p>";
  page += "<p class='data'>LED: <span id='led'></span></p>";
  page += "<script>";
  page += "async function update(){";
  page += "let res=await fetch('/status');";
  page += "let d=await res.json();";
  page += "document.getElementById('wifi').innerHTML=d.wifi;";
  page += "document.getElementById('temp').innerHTML=d.temp;";
  page += "document.getElementById('hum').innerHTML=d.hum;";
  page += "document.getElementById('pir').innerHTML=d.motion?'Gerakan Terdeteksi':'Tidak Ada Gerakan';";
  page += "document.getElementById('pir').className=d.motion?'on':'off';";
  page += "document.getElementById('led').innerHTML=d.led?'ON':'OFF';";
  page += "document.getElementById('led').className=d.led?'on':'off';";
  page += "}";
  page += "setInterval(update,1000);update();";
  page += "</script></body></html>";
  return page;
}

void handleRoot() { //untuk memanggil halaman website ketika kita memasukkan ip address
  server.send(200, "text/html", htmlPage());//memberikan perintah untuk membuka halaman website
}

void handleStatus() { //mengubah status nilai-nilai sensor secara real-time
  String json = "{";
  json += "\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? "Terhubung" : "Terputus") + "\",";
  json += "\"temp\":" + String(temperature,1) + ",";
  json += "\"hum\":" + String(humidity,1) + ",";
  json += "\"motion\":" + String(motion_state) + ",";
  json += "\"led\":" + String(ledState ? 1 : 0);
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() { //kalau salah ip address
  server.send(404, "text/plain", "Not found");
}

// ===================== SETUP ===========================
void setup() {
  Serial.begin(115200); //serial kecepatan komunikasi
  delay(10); //jeda proses

  antares.setDebug(true); //munculkan serial di monitor
  antares.wifiConnection(ssid, password);
  antares.setMqttServer();
  wifi.begin(ssid,password); //cara umumnya

  pinMode(PIR_PIN, INPUT); //esp yang menerima
  pinMode(LED_PIN, OUTPUT); //esp yang memberikan/mengeluarkan
  dht.begin(); //agar semua sensor dht kepakai

  Serial.print("Menghubungkan ke WiFi"); //keterangaan
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  //menghubungkan ke wifi
  Serial.println();
  Serial.print("WiFi Terhubung, IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
}

// ===================== LOOP ============================
void loop() {
  server.handleClient(); //supaya browser tau yang minta itu esp8266
  antares.checkMqttConnection(); //menyambungkan antares ke esp

  // Baca sensor
  motion_state = digitalRead(PIR_PIN); //misal nilai pin sensor pir itu high atau satu, maka nilai dari motion state akan berubah dari low menjadi high
  humidity = dht.readHumidity(); //nilai humidity yang awalnya 0, akan berubah sesuai nilai asli dari sensor
  temperature = dht.readTemperature(); //nilai temperatur yang awalnya 0, akan berubah sesuao nilai asli dari sensor

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("⚠ Gagal membaca DHT");
    return;
  }

  // Kontrol LED
  if (motion_state == HIGH && temperature >= 30) {
    digitalWrite(LED_PIN, HIGH);
    ledState = true;//led-=on
  } else {
    digitalWrite(LED_PIN, LOW);
    ledState = false;//led=off
  }

  // Kirim ke Antares setiap 5 detik
  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();
    antares.add("temperature", temperature);
    antares.add("humidity", humidity);
    antares.add("motion", motion_state);
    antares.add("led", ledState);
    antares.publish(PROJECT_NAME, DEVICE_NAME); //publish data sesuai nama project dan device
    Serial.println("✅ Data terkirim ke Antares");
  }

  delay(200);
}
