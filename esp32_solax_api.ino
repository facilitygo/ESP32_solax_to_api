#include <WiFi.h>
#define LED_BUILTIN 2  // Define LED_BUILTIN for ESP32
#include <HTTPClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

WiFiUDP ntpUDP;

String solax_ip = "local.solax.ip";  // Defaultní IP adresa Solaxu - If solax change IP adress you may change it in your db at API server
const char* token_url = "https://your-api-server.com/token";
const char* ip_url = "https://your-api-server.com/ip";
const char* data_url = "https://your-api-server.com/data/";
const char* ntpServer = "pool.ntp.org";   // NTP server pro synchronizaci času
const int timeZoneOffset = 3600;          // Offset časového pásma v sekundách (např. 3600 pro GMT+1)

String username = "";
String password_auth = "";
String token = "";

// Adresy v EEPROM
#define EEPROM_SIZE 256
#define SSID_ADDR 0
#define PASSWORD_ADDR 33  // SSID (32) + 1 mezera = 33
#define USERNAME_ADDR 98  // Heslo WiFi (64) + 1 mezera = 98
#define PASSWORD_AUTH_ADDR 130  // Username API (32) + 1 mezera = 130

// BLE nastavení UUID
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *pCharacteristic;

// Funkce pro synchronizaci času
void syncTime() {
  configTime(timeZoneOffset, 0, ntpServer);
  while (time(nullptr) < 100000) {
    delay(100);
  }
}

// Funkce pro zápis do EEPROM
void writeEEPROM(int addr, const char* data, int len) {
  for (int i = 0; i < len; i++) {
    if (i < strlen(data)) {
      EEPROM.write(addr + i, data[i]);
    } else {
      EEPROM.write(addr + i, 0);
    }
  }
  EEPROM.commit();
}

// Funkce pro čtení z EEPROM
void readEEPROM(int addr, char* data, int len) {
  for (int i = 0; i < len; i++) {
    data[i] = EEPROM.read(addr + i);
    if (data[i] == 0) break;
  }
  data[len - 1] = '\0';
}

// Callback třída pro sledování připojení a příjmu dat
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = String(pCharacteristic->getValue().c_str());

      if (value.length() > 0) {
        String receivedData = value;

        int separatorIndex1 = receivedData.indexOf(':');
        int separatorIndex2 = receivedData.indexOf(':', separatorIndex1 + 1);
        int separatorIndex3 = receivedData.indexOf(':', separatorIndex2 + 1);

        if (separatorIndex1 != -1 && separatorIndex2 != -1 && separatorIndex3 != -1) {
          String newSSID = receivedData.substring(0, separatorIndex1);
          String newPassword = receivedData.substring(separatorIndex1 + 1, separatorIndex2);
          String newUsername = receivedData.substring(separatorIndex2 + 1, separatorIndex3);
          String newPasswordAuth = receivedData.substring(separatorIndex3 + 1);
          
          Serial.println("Received SSID: " + newSSID);
          Serial.println("Received WiFi Password: " + newPassword);
          Serial.println("Received API Username: " + newUsername);
          Serial.println("Received API Password: " + newPasswordAuth);

          // Uložení do EEPROM
          writeEEPROM(SSID_ADDR, newSSID.c_str(), 32);
          writeEEPROM(PASSWORD_ADDR, newPassword.c_str(), 64);
          writeEEPROM(USERNAME_ADDR, newUsername.c_str(), 32);
          writeEEPROM(PASSWORD_AUTH_ADDR, newPasswordAuth.c_str(), 32);
          EEPROM.commit();

          // Připojení k WiFi
          WiFi.begin(newSSID.c_str(), newPassword.c_str());
          int attempt = 0;
          while (WiFi.status() != WL_CONNECTED && attempt < 20) {
            delay(500);
            Serial.print(".");
            attempt++;
          }

          if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWi-Fi připojeno!");
            username = newUsername;
            password_auth = newPasswordAuth;
            syncTime();
          } else {
            Serial.println("\nNepodařilo se připojit k Wi-Fi.");
          }
        } else {
          Serial.println("Invalid format. Expected format: SSID:WiFiPassword:APIUsername:APIPassword");
        }
      }
    }
};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // Initialize LED pin
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on Bluetooth LED by default
  Serial.begin(115200);

  // Inicializace BLE s názvem zařízení
  BLEDevice::init("ESP32_Solax_Device");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("Waiting for WiFi and API credentials...");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  digitalWrite(LED_BUILTIN, LOW);  // Turn off Bluetooth LED after advertising starts
  Serial.println("BLE device is now discoverable.");

  WiFi.mode(WIFI_STA);
  EEPROM.begin(EEPROM_SIZE);

  char storedSSID[32] = "";
  char storedPassword[64] = "";
  char storedUsername[32] = "";
  char storedPasswordAuth[32] = "";

  readEEPROM(SSID_ADDR, storedSSID, 32);
  readEEPROM(PASSWORD_ADDR, storedPassword, 64);
  readEEPROM(USERNAME_ADDR, storedUsername, 32);
  readEEPROM(PASSWORD_AUTH_ADDR, storedPasswordAuth, 32);

  if (strlen(storedSSID) == 0 || strlen(storedPassword) == 0 || strlen(storedUsername) == 0 || strlen(storedPasswordAuth) == 0) {
    Serial.println("SSID, heslo nebo API údaje nejsou uloženy. Zadejte údaje přes BLE.");
  } else {
    WiFi.begin(storedSSID, storedPassword);
digitalWrite(LED_BUILTIN, LOW);  // Turn off Bluetooth LED when connected to WiFi
    username = String(storedUsername);
    password_auth = String(storedPasswordAuth);

    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
      delay(500);
      attempt++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      syncTime();
    } else {
      Serial.println("Nepodařilo se připojit k Wi-Fi.");
    }
  }
}

void loop() {
  if (token == "") {
    token = getToken();
  }

  String response = connect_Solax();
  
  if (response.length() > 0) {
    sendToApi(response);  // Odesíláme surová data přímo na API
  }
  
  delay(60000);
}

// Funkce pro získání tokenu
String getToken() {
  while (WiFi.status() == WL_CONNECTED && token == "") {
    HTTPClient http;
    http.begin(token_url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Sestavení formulářového payloadu pro přihlášení
    String authPayload = "username=" + username + "&password=" + password_auth;
    
    Serial.println("Sending token request...");
    int httpResponseCode = http.POST(authPayload);

    if (httpResponseCode == 200) {
      String response = http.getString();
      int startIndex = response.indexOf("\"access_token\":\"") + 16;
      int endIndex = response.indexOf("\"", startIndex);
      token = response.substring(startIndex, endIndex);
      Serial.println("Token acquired: " + token);
    } else {
      Serial.print("Failed to acquire token. HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.println("Response: " + http.getString());
      Serial.println("Retrying token request in 5 seconds...");
      delay(10000); // Zpoždění před dalším pokusem (10 sekund)
    }
    http.end();
  }
  return token;
}

// Funkce pro připojení k Solax a získání dat
String connect_Solax() {
  int attempt = 0;
  while (attempt < 10) {  // Pokusíme se 10x připojit k Solax
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = String("http://") + solax_ip;
      
      http.begin(url);
      http.addHeader("X-Forwarded-For", solax_ip);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");

      String postData = "optType=ReadRealTimeData&pwd=232121"; //default Solax password
      Serial.println("Connecting to Solax...");
      int httpResponseCode = http.POST(postData);
      
      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Received data from Solax:");
        Serial.println(response);
        http.end();
        return response;
      } else {
        Serial.print("Error in POST request to Solax (attempt ");
        Serial.print(attempt + 1);
        Serial.print("/10): ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
      }
      http.end();
      attempt++;
      delay(10000); // Zpoždění mezi pokusy (10 sekund)
    }
  }
  
  // Pokud po 10 pokusech není Solax dostupný, získáme novou IP adresu
  Serial.println("Failed to connect to Solax after 10 attempts. Requesting new IP...");
  updateSolaxIp();
  return "";
}

// Funkce pro získání nové IP adresy Solax ze serveru API
void updateSolaxIp() {
  if (WiFi.status() == WL_CONNECTED && token != "") {
    HTTPClient http;
    String ipUrl = String(ip_url) + "?username=" + username;
    http.begin(ipUrl);
    http.addHeader("Authorization", "Bearer " + token);
    
    Serial.println("Requesting new IP address for Solax...");
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("New Solax IP address response: " + response);

      // Extrakce IP adresy z JSON odpovědi
      int startIndex = response.indexOf("\"ip\":\"") + 6;
      int endIndex = response.indexOf("\"", startIndex);
      if (startIndex != -1 && endIndex != -1) {
        solax_ip = response.substring(startIndex, endIndex);
        Serial.println("New Solax IP address acquired: " + solax_ip);
      } else {
        Serial.println("Failed to parse IP address from response.");
      }
    } else {
      Serial.print("Failed to retrieve new IP. HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.println("Response: " + http.getString());
    }
    http.end();
  }
}

// Funkce pro odesílání surových dat na API
void sendToApi(String rawData) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(data_url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    Serial.println("Sending data to API...");
    int postResponseCode = http.POST(rawData);  // Odesíláme surová data přímo na API
    
    if (postResponseCode > 0) {
      String response = http.getString();
      Serial.print("POST Response Code: ");
      Serial.println(postResponseCode);
      Serial.println("Response from API:");
      Serial.println(response);
    } else {
      Serial.print("Error in POST request to API: ");
      Serial.println(http.errorToString(postResponseCode).c_str());
    }
    http.end();
  }
}

unsigned long now() {
  time_t now;
  time(&now);
  return now;
}
