#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Q2HX711.h>
#include <Adafruit_BMP085.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ArduinoJson.h>
#include "secrets.h"

#define HOSTNAME "topasik"

Q2HX711 psens1(D0, D4); // data on D0, clock on D4
Q2HX711 psens2(D5, D6); // data on D5, clock on D6
Adafruit_BMP085 bmp; // uses pins D1 as SCL and D2 as SDA

BearSSL::WiFiClientSecure secureClient;
ESP8266WebServer webServer(80);

void handleRoot();
void handleNotFound();

void initOTA(){
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPasswordHash(OTA_UPDATE_PASSWORD_HASH);
  ArduinoOTA.begin();
}

void initWiFi() {
  WiFiManager wifiManager;
  wifiManager.setTimeout(600);
  if (!wifiManager.autoConnect(HOSTNAME, WIFI_MANAGER_PASSWORD)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  Serial.println("connected to WiFi");
  Serial.println("local ip: ");
  Serial.println(WiFi.localIP());  
}

float p1, p2;
int current;
float temp, pressure;
bool bmpStarted;

void initSensors(){
  bmpStarted = bmp.begin();
  if (!bmpStarted) 
  {
    Serial.println("Error starting BMP180 sensor");
  }    
}

// Set time via NTP, as required for x.509 validation
void setClock() {
  const time_t minTimestamp = 8*3600;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  for (int i = 0; i < 100 && now < minTimestamp; i++) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  if (now < minTimestamp) {
    Serial.println("Failed to set time via NTP");
  }
  Serial.println("Current time: " + String(now));
}

void initSecureClient() {
  static const char digicert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT
DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow
PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD
Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB
AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O
rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq
OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b
xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw
7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD
aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV
HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG
SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69
ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr
AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz
R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5
JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo
Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ
-----END CERTIFICATE-----
)EOF";
  BearSSL::X509List cert(digicert);
  secureClient.setTrustAnchors(&cert);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  initWiFi();
  initOTA();
  initSensors();
  webServer.on("/", handleRoot);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  setClock();
  initSecureClient();
  delay(1000);
  Serial.println("Setup finished.");  
}

int measureCurrent(){
    int i;
    const int cnt = 10;
    int minv = 1025;
    int maxv = -1;
    for(i = 0; i < cnt; i++) {
      int value = analogRead(A0);
      if (value > maxv) {
        maxv = value;
      }
      if (value < minv) {
        minv = value;
      }
      delay(2); 
    }
    return maxv - minv;
}

void printValues(){
  Serial.print("p1: "); Serial.println(p1, 0);
  Serial.print("p2: "); Serial.println(p2, 0);
  Serial.print("temp: "); Serial.println(temp);
  Serial.print("press: "); Serial.println(pressure);
  Serial.print("current: "); Serial.println(current);
  Serial.print("\n");
  Serial.flush();
  
}

void loop() {
    for(int i = 0; i < 10; i++) {
      webServer.handleClient(); // Listen for HTTP requests from clients
      ArduinoOTA.handle();      
      delay(50);
    }  
    p1 = psens1.read()/100.0;
    p2 = psens2.read()/100.0;
    if(bmpStarted){
      temp = bmp.readTemperature();
      pressure = bmp.readPressure()/100;     
    }
    current = measureCurrent();
    printValues();
}

void handleRoot() {
  StaticJsonDocument<128> doc;
  doc["ts"] = time(nullptr);
  doc["lp1"] = p1;
  doc["lp2"] = p2;
  doc["temp"] = temp;
  doc["pressure"] = pressure;
  doc["current"] = current;
  char body[128];
  serializeJson(doc, body);
  webServer.send(200, "application/json", body);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  webServer.send(404, "text/plain", message);
}
