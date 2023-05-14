#include <M5StickCPlus.h>
#include "secrets.h"
#include "WiFi.h"
#include "AsyncUDP.h"

// button variables
bool buttonA = false;
bool buttonB = false;

// Display variables
char statusMsg[50];

// Time-related variables
int64_t secondsSinceStart = 0;
int64_t nextSecondTime;

// Network variables
bool wifiConnecting = false;
bool wifiConnected = false;
bool udpListening = false;
bool wifiSetupComplete = false;

const int udpPort = 6970;

IPAddress linkyM5IP(192, 168, 8, 10);
IPAddress gateway(192, 168, 8, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   // optional
IPAddress secondaryDNS(8, 8, 4, 4); // optional

AsyncUDP udp;


bool connectWifi()
{
  if (!wifiConnecting)
  {
    Serial.println("working on setting up networking");
    // runs once
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiConnecting = true;
    return false;
  }

  if (!wifiConnected)
  {
    // polled until wifi connects
    if (WiFi.status() != WL_CONNECTED)
    {
      return false;
    }
    wifiConnected = true;
    M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
    Serial.print("connected to wifi with IP: ");
    Serial.println(WiFi.localIP());
    return false;
  }

  if (!udpListening)
  {
    // set up lambda to call on receipt of a udp packet
    Serial.println("Setting up UDP server");
    if (udp.listen(udpPort))
    {
      Serial.print("UDP listening on port ");
      Serial.println(udpPort);
      udp.onPacket([](AsyncUDPPacket packet)
      {
        // this is a bloody lambda!
        Serial.print("Received packet from ");
        Serial.print(packet.remoteIP());
        Serial.print(", Length: ");
        Serial.print(packet.length());
        Serial.print(", Data: ");
        Serial.write(packet.data(), packet.length());
        Serial.println();
        Serial.write(packet.data(), packet.length());
        Serial.println();
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0, 2);
        M5.Lcd.print((char*)packet.data());
      });
      udpListening = true;
      M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    }
  }
  return true;
}

void secondsUpdate()
{
  bool rv;
  // Check if we're connected to Wifi yet
  if (!wifiSetupComplete)
  {
    wifiSetupComplete = connectWifi();
  }

  if (buttonA)
  {
    udp.writeTo((uint8_t*)"toggle", 6, linkyM5IP, udpPort);
    buttonA = false;
    Serial.println("ButtonA toggled power");
  }

  if (wifiSetupComplete)
  {
    Serial.println("Writing status request to linkyM5");
    udp.writeTo((uint8_t*)"status", 6, linkyM5IP, udpPort);
  }
}

void setup() {
  // initialize M5StickC
  M5.begin();
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.update();
  Serial.begin(115200);

  // initialize time
  int64_t now = esp_timer_get_time();
  nextSecondTime = now + 1000000;  // time to increment the seconds counter
}

void loop() {
  // Read buttons
  M5.update();
  if (M5.BtnA.wasReleased())
  {
    buttonA = true;
  }
  if (M5.BtnB.wasReleased())
  {
    buttonB = true;
  }

  // Check if it's time to increment the seconds-counter
  // and process everything that should be done each second
  int64_t now_us = esp_timer_get_time();
  if (now_us > nextSecondTime)
  {
    secondsSinceStart++;
    nextSecondTime += 1000000;
    // Serial.printf("%lld toggleTime: %d state %d\n", secondsSinceStart, nextFlowToggleTime, generateFlowPulses);
    secondsUpdate();
  }
}
