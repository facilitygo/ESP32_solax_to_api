#include <WiFi.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#define LED_BUILTIN 2  // Define LED_BUILTIN for ESP32
WiFiUDP ntpUDP;

String solax_ip = "";  // Defaultní IP adresa Solaxu
const char* token_url = "https://apina.energyai.uk:8010/token";
const char* ip_url = "https://apina.energyai.uk:8010/ip";
const char* data_url = "https://apina.energyai.uk:8010/data/";
const char* ntpServer = "pool.ntp.org";   // NTP server pro synchronizaci času
const int timeZoneOffset = 3600;          // Offset časového pásma v sekundách (např. 3600 pro GMT+1)

String username = "";
String password_auth = "";
String token = "";
String solaxPassword = "";

// Adresy v EEPROM
#define EEPROM_SIZE 256
#define SSID_ADDR 0
#define PASSWORD_ADDR 33  // SSID (32) + 1 mezera = 33
#define USERNAME_ADDR 98  // Heslo WiFi (64) + 1 mezera = 98
#define PASSWORD_AUTH_ADDR 130  // Username API (32) + 1 mezera = 130
#define SOLAX_IP_ADDR 162        // Password Auth (32) + 1 mezera = 162
#define SOLAX_PASSWORD_ADDR 194 // Solax IP (32) + 1 mezera = 194

// BLE nastavení UUID
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Prototypes of functions used in the code
String getToken();
String connect_Solax();
void updateSolaxIp();
void sendToApi(String rawData);

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
        int separatorIndex4 = receivedData.indexOf(':', separatorIndex3 + 1);
        int separatorIndex5 = receivedData.indexOf(':', separatorIndex4 + 1);

        if (separatorIndex1 != -1 && separatorIndex2 != -1 && separatorIndex3 != -1 &&
            separatorIndex4 != -1 && separatorIndex5 != -1) {
            String newSSID = receivedData.substring(0, separatorIndex1);
            String newPassword = receivedData.substring(separatorIndex1 + 1, separatorIndex2);
            String newUsername = receivedData.substring(separatorIndex2 + 1, separatorIndex3);
            String newPasswordAuth = receivedData.substring(separatorIndex3 + 1, separatorIndex4);
            String newSolaxIp = receivedData.substring(separatorIndex4 + 1, separatorIndex5);
            String newSolaxPassword = receivedData.substring(separatorIndex5 + 1);

            Serial.println("Received SSID: " + newSSID);
            Serial.println("Received WiFi Password: " + newPassword);
            Serial.println("Received API Username: " + newUsername);
            Serial.println("Received API Password: " + newPasswordAuth);
            Serial.println("Received Solax IP: " + newSolaxIp);
            Serial.println("Received Solax Password: " + newSolaxPassword);

            // Uložení do EEPROM
            writeEEPROM(SSID_ADDR, newSSID.c_str(), 32);
            writeEEPROM(PASSWORD_ADDR, newPassword.c_str(), 64);
            writeEEPROM(USERNAME_ADDR, newUsername.c_str(), 32);
            writeEEPROM(PASSWORD_AUTH_ADDR, newPasswordAuth.c_str(), 32);
            writeEEPROM(SOLAX_IP_ADDR, newSolaxIp.c_str(), 32);
            writeEEPROM(SOLAX_PASSWORD_ADDR, newSolaxPassword.c_str(), 32);
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
                solax_ip = newSolaxIp;
                solaxPassword = newSolaxPassword;
                syncTime();
                digitalWrite(LED_BUILTIN, LOW);  // Turn off Bluetooth LED when connected to WiFi
            } else {
                Serial.println("\nNepodařilo se připojit k Wi-Fi.");
            }
        } else {
            Serial.println("Invalid format. Expected format: SSID:WiFiPassword:APIUsername:APIPassword:SolaxIP:SolaxPassword");
        }
    }
}
};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // Initialize LED pin
  //digitalWrite(LED_BUILTIN, HIGH);  // Turn on Bluetooth LED by default
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(115200);

  // Kontrola, zda jsou v EEPROM uložená data pro WiFi
  EEPROM.begin(EEPROM_SIZE);

  char storedSSID[32] = "";
  char storedPassword[64] = "";
  char storedUsername[32] = "";
  char storedPasswordAuth[32] = "";
  char storedSolaxIp[32] = "";
  char storedSolaxPassword[32] = "";

  readEEPROM(SSID_ADDR, storedSSID, 32);
  readEEPROM(PASSWORD_ADDR, storedPassword, 64);
  readEEPROM(USERNAME_ADDR, storedUsername, 32);
  readEEPROM(PASSWORD_AUTH_ADDR, storedPasswordAuth, 32);
  readEEPROM(SOLAX_IP_ADDR, storedSolaxIp, 32);
  readEEPROM(SOLAX_PASSWORD_ADDR, storedSolaxPassword, 32);

  if (strlen(storedSSID) == 0 || strlen(storedPassword) == 0) {
    Serial.println("WiFi credentials are missing. Waiting for input...");
    while (strlen(storedSSID) == 0 || strlen(storedPassword) == 0) {
      delay(10000);  // Wait for 10 seconds
      Serial.println("Still waiting for WiFi credentials via BLE...");
      readEEPROM(SSID_ADDR, storedSSID, 32);
      readEEPROM(PASSWORD_ADDR, storedPassword, 64);
    }
  } else {
    Serial.println("WiFi credentials found in EEPROM.");
    WiFi.begin(storedSSID, storedPassword);
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
      delay(500);
      attempt++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi successfully.");
      syncTime();
      digitalWrite(LED_BUILTIN, LOW);  // Turn off Bluetooth LED when connected to WiFi
    } else {
      Serial.println("Failed to connect to WiFi.");
    }
  }

  if (strlen(storedUsername) == 0 || strlen(storedPasswordAuth) == 0) {
    Serial.println("API credentials are missing.");
  } else {
    Serial.println("API credentials found in EEPROM.");
    username = String(storedUsername);
    password_auth = String(storedPasswordAuth);
  }

  if (strlen(storedSolaxIp) > 0) {
    solax_ip = String(storedSolaxIp);
}

if (strlen(storedSolaxPassword) > 0) {
    solaxPassword = String(storedSolaxPassword);
}

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
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && token == "") {
    Serial.println("Requesting new token...");
    token = getToken();
  }

  String response = connect_Solax();
  
  if (response.length() > 0) {
    sendToApi(response);  // Odesíláme surová data přímo na API
  } else if (WiFi.status() == WL_CONNECTED && response.length() == 0) {
    Serial.println("Token expired or invalid. Requesting new token...");
    token = getToken();
    if (token != "") {
      sendToApi(response);  // Odesíláme surová data znovu po obnovení tokenu
    }
  }
  
  delay(60000);
}

// Funkce pro získání tokenu
String getToken() {
  Serial.println("token requestik...");
  String token = "";
  int attempts = 0;
  const int maxAttempts = 10; // Maximální počet pokusů

  while (WiFi.status() == WL_CONNECTED && token == "") {
    
    Serial.println("token requestik..." + String(token_url));
    HTTPClient http;
    http.begin(token_url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Sestavení formulářového payloadu pro přihlášení
    String authPayload = "username=" + username + "&password=" + password_auth;
    
    Serial.println("Sending token request..." + String(authPayload));
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
      attempts++;
      if (attempts < maxAttempts) {
        Serial.println("Retrying token request in 10 seconds...");
        delay(10000); // Zpoždění před dalším pokusem (10 sekund)
      } else {
        Serial.println("Maximum retry attempts reached. Restarting device...");
        delay(1000); // Krátké zpoždění před restartem pro dokončení serial výpisu
        ESP.restart(); // Softwarový restart zařízení
      }
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

      String postData = "optType=ReadRealTimeData&pwd=" + String(solaxPassword) ;
      Serial.println("Connecting to Solax..." + String(solax_ip));
      Serial.println("Connecting to Solax..." + String(postData));
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
    http.setTimeout(15000); // Nastavení timeoutu na 15 sekund
    http.begin(data_url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    Serial.println("Sending data to API...");
    int postResponseCode = http.POST(rawData);  // Odesíláme surová data přímo na API
    
    if (postResponseCode == 401) {
      Serial.println("Unauthorized (401). Requesting new token...");
      token = getToken();
      if (token != "") {
        Serial.println("Retrying to send data with new token...");
        http.begin(data_url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + token);
        postResponseCode = http.POST(rawData);
      }
    }

    // Zpracování výsledku po timeoutu
    if (postResponseCode == HTTPC_ERROR_READ_TIMEOUT) {
      Serial.println("Read Timeout occurred. Retrying immediately...");
      http.begin(data_url);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Authorization", "Bearer " + token);
      postResponseCode = http.POST(rawData);
    }

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
