#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <mbedtls/aes.h>
#include <base64.h>
#include <WiFiManager.h>  // Library WiFiManager

const char* ssid = "BOLEH";                                             // Ganti dengan nama jaringan Wi-Fi Anda
const char* password = "";                                              // Ganti dengan password Wi-Fi Anda
const char* serverUrl = "https://simover.web.id/api/sensor-histories";  // Ganti dengan endpoint Laravel Anda

// AES key (16 bytes for AES-128)
uint8_t key[] = { 0x00, 0x01, 0x02,....., 0x0F };

int deviceId = 1000000001;

// Konfigurasi DHT22
#define DHTPIN 13      // Pin data DHT22
#define DHTTYPE DHT22  // Tipe sensor DHT22
DHT dht(DHTPIN, DHTTYPE);

// Konfigurasi MQ2
#define MQ2_PIN 34  // Pin analog untuk MQ2

// Konfigurasi PIR
#define PIR_PIN 32  // Pin digital untuk PIR

#define RESET_PIN 15  //pin reset

WiFiManager wm;

HTTPClient http;

void setup() {
  Serial.begin(115200);

  // Set pin 2 sebagai OUTPUT untuk LED
  pinMode(2, OUTPUT);

  pinMode(RESET_PIN, INPUT_PULLUP);  // Konfigurasi pin reset WiFi

  // Koneksi ke WiFi
  // Coba untuk menghubungkan ke Wi-Fi yang tersimpan sebelumnya
  if (wm.autoConnect()) {
    // Jika terhubung ke Wi-Fi
    digitalWrite(2, LOW);  // Matikan LED jika sudah terhubung ke WiFi
    Serial.println("Tersambung ke Wi-Fi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    // Jika gagal menghubungkan, masuk ke portal konfigurasi
    Serial.println("Gagal menghubungkan ke Wi-Fi, masuk ke mode AP...");
    for (int i = 0; i < 10; i++) {
      digitalWrite(2, HIGH);
      delay(200);
      digitalWrite(2, LOW);
      delay(200);
      digitalWrite(2, HIGH);
    }
    wm.startConfigPortal("SimoverApp", "");  // Portal Wi-Fi
    Serial.println("Mode AP aktif, membuka portal konfigurasi...");
  }

  // Inisialisasi DHT22
  dht.begin();

  // Inisialisasi PIR sebagai input
  pinMode(PIR_PIN, INPUT);
}

void loop() {

  if (digitalRead(RESET_PIN) == LOW) {
    Serial.println("Tombol reset WiFi ditekan, membuka portal konfigurasi...");

    // Menyalakan LED saat tombol reset ditekan
    for (int i = 0; i < 10; i++) {
      digitalWrite(2, HIGH);
      delay(200);
      digitalWrite(2, LOW);
      delay(200);
      digitalWrite(2, HIGH);
    }

    delay(1000);                                               // Debounce
    wm.resetSettings();                                        // Reset semua kredensial WiFi tersimpan
    wm.startConfigPortal("SimoverApp", "");  // Portal Wi-Fi

    Serial.println("Konfigurasi selesai, restart ESP32.");
    ESP.restart();  // Restart setelah konfigurasi
  }

  if (WiFi.status() != WL_CONNECTED) {
    // Jika tidak terhubung, LED berkedip dan masuk ke mode AP
    Serial.println("Koneksi Wi-Fi terputus, masuk ke mode AP...");

    for (int i = 0; i < 10; i++) {
      digitalWrite(2, HIGH);
      delay(200);
      digitalWrite(2, LOW);
      delay(200);
      digitalWrite(2, HIGH);
    }

    // Buka portal konfigurasi untuk mengatur ulang Wi-Fi
    wm.startConfigPortal("SimoverApp", "");

    Serial.println("Mode AP aktif, membuka portal konfigurasi...");
  } else {
    // Jika terhubung, pastikan LED mati
    digitalWrite(2, LOW);
    Serial.println("Wi-Fi terhubung, LED mati.");
  }

  // Membaca data dari DHT22
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Membaca data dari MQ2
  int smokeLevel = analogRead(MQ2_PIN);

  // Membaca data dari PIR
  bool motionDetected = digitalRead(PIR_PIN);  // true jika gerakan terdeteksi

  // Validasi hasil pembacaan
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Gagal membaca data dari DHT22!");
  } else {
    // Menampilkan data
    Serial.print("Suhu: ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("Kelembapan: ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("Level Asap: ");
    Serial.println(smokeLevel);

    Serial.print("Gerakan Terdeteksi: ");
    Serial.println(motionDetected ? "Ya" : "Tidak");

    if (motionDetected) {
      digitalWrite(2, HIGH);  // Menyalakan LED jika gerakan terdeteksi
      Serial.println("Peringatan: Gerakan terdeteksi!");
    } else {
      digitalWrite(2, LOW);  // Mematikan LED jika tidak ada gerakan
    }

    // Kirim data ke server
    sendDataToApi(temperature, humidity, smokeLevel, motionDetected);
  }

  // Jeda sebelum pembacaan berikutnya
  delay(5000);
}

// Fungsi untuk mengirim data ke server menggunakan POST
void sendDataToApi(float temperature, float humidity, int smokeLevel, bool motionDetected) {
  if (WiFi.status() == WL_CONNECTED) {
    // Inisialisasi koneksi HTTP
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");  // Header untuk JSON

    // Membuat payload JSON
    String jsonPayload = "{";
    jsonPayload += "\"device_id\": " + String(deviceId) + ",";
    jsonPayload += "\"temperature\": " + String(temperature) + ",";
    jsonPayload += "\"humidity\": " + String(humidity) + ",";
    jsonPayload += "\"smoke\": " + String(smokeLevel) + ",";
    jsonPayload += "\"motion\": " + String(motionDetected ? "true" : "false");
    jsonPayload += "}";

    // Enkripsi JSON payload
    String encryptedPayload = encryptAES(jsonPayload);
    Serial.print("Encrypted Payload: ");
    Serial.println(encryptedPayload);

    // Mengirim permintaan POST
    int httpResponseCode = http.POST(encryptedPayload);

    // Memeriksa respons server
    if (httpResponseCode > 0) {
      Serial.print("Data terkirim! HTTP Response code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Gagal mengirim data. Error code: ");
      Serial.println(httpResponseCode);
    }

    // Menutup koneksi HTTP
    http.end();
  } else {
    Serial.println("WiFi tidak terhubung!");
  }
}

String encryptAES(String input) {
  size_t paddedLength = ((input.length() + 15) / 16) * 16;
  uint8_t plaintext[paddedLength];
  uint8_t ciphertext[paddedLength];

  memset(plaintext, 0, paddedLength);
  memcpy(plaintext, input.c_str(), input.length());

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);

  for (size_t i = 0; i < paddedLength; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, &plaintext[i], &ciphertext[i]);
  }

  mbedtls_aes_free(&aes);
  return base64::encode(ciphertext, paddedLength);
}
