#include <M5StickCPlus.h>
#include "secrets.h"
#include "WiFi.h"
#include "AsyncUDP.h"
#include <string>
#include <EEPROM.h>
#include <ESP32Ping.h>


// button variables
bool buttonA = false;
bool buttonB = false;

// Display variables
char statusMsg[50];
bool haltDisplayUpdate = false;

// Time-related variables
int64_t secondsSinceStart = 0;
int64_t nextSecondTime;
const int initialPoweroffTime = 120;
const int longPoweroffTime = 300;
int poweroffTimer = initialPoweroffTime;

// Network variables
bool wifiScanned = false;
bool wifiConnecting = false;
bool wifiConnected = false;
bool udpListening = false;
bool wifiSetupComplete = false;
String chosenNetwork;
bool pingOk = false;

const int udpPort = 6970;

IPAddress linkyM5IP(192, 168, 8, 10);
IPAddress linkyIP(192, 168, 100, 1);
IPAddress gateway(192, 168, 8, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   // optional
IPAddress secondaryDNS(8, 8, 4, 4); // optional

struct PingTarget {
  String displayHostname;
  bool useIP;  // ping by IP if true, by FQN if false
  IPAddress pingIP;
  String fqn;
  bool pinged;  // Have sent a ping to this host
  bool pingOK;  // result of ping
};
PingTarget linkyRouter = {.displayHostname="linkyRouter", .useIP=true, .pingIP=gateway, .pinged=false, .pingOK=false};
PingTarget linkyM5 = {.displayHostname="linkyM5stick", .useIP=true, .pingIP=linkyM5IP, .pinged=false, .pingOK=false};
PingTarget linky = {.displayHostname="linky", .useIP=true, .pingIP=linkyIP, .pinged=false, .pingOK=false};
PingTarget dns = {.displayHostname="DNS server", .useIP=true, .pingIP=primaryDNS, .pinged=false, .pingOK=false};
PingTarget google = {.displayHostname="google", .useIP=false, .fqn="www.google.com", .pinged=false, .pingOK=false};

PingTarget* pingTargetArray[] = {&linkyRouter, &linkyM5, &linky, &dns, &google};
int pingTargetArrayLen = sizeof(pingTargetArray) / sizeof(&linkyRouter);
int pingTargetNum = 0;

AsyncUDP udp;

String chooseNetwork()
{
  String chosenNetwork = "";
  Serial.println("Scan start");

  // WiFi.scanNetworks will return the number of networks found.
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");
  if (n == 0)
  {
    Serial.println("no networks found");
    return(chosenNetwork);
  }
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    Serial.println("Nr | SSID");
    for (int i = 0; i < n; ++i)
    {
      String SSID = WiFi.SSID(i);
      // Print SSID for each network found
      Serial.printf("%2d",i + 1);
      Serial.print(" | ");
      Serial.printf("%-32.32s\n", SSID.c_str());
      if (!SSID.compareTo("129Linky"))
        chosenNetwork = SSID;
    }
  }

  Serial.println("");

  // Delete the scan result to free memory
  WiFi.scanDelete();

  return chosenNetwork;
}

bool connectWifi()
{
  if (!wifiScanned)
  {
    chosenNetwork = chooseNetwork();
    if (chosenNetwork.compareTo("") == 0)
    {
      return false;
    }
    else
    {
      Serial.print("connecting to network: ");
      Serial.println(chosenNetwork);
      wifiScanned = true;
    }
  }

  if (!wifiConnecting)
  {
    Serial.println("working on setting up networking");
    // runs once
    WiFi.begin(chosenNetwork.c_str(), WIFI_PASSWORD);
    wifiConnecting = true;
    return false;
  }

  if (!wifiConnected)
  {
    // polled until wifi connects
    if (WiFi.status() != WL_CONNECTED)
    {
      displayStatus("connecting");
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
        bool verbose = false;
        if (verbose)
        {
          Serial.print("Received packet from ");
          Serial.print(packet.remoteIP());
          Serial.print(", Length: ");
          Serial.print(packet.length());
          Serial.print(", Data: ");
          Serial.write(packet.data(), packet.length());
          Serial.println();
        }
        displayStatus((char*)packet.data());
      });
      udpListening = true;
      M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    }
  }
  return true;
}

void displayStatus(String msg)
{
  if (haltDisplayUpdate)
    return;
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.print(msg.c_str());
  displayPing();
}

void doPing()
{
  PingTarget* pingTarget_p = pingTargetArray[pingTargetNum];
  if (pingTarget_p->useIP)
  {
    pingTarget_p->pingOK = Ping.ping(pingTarget_p->pingIP, 1);
  }
  else
  {
    pingTarget_p->pingOK = Ping.ping(pingTarget_p->fqn.c_str(), 1);
  }
  pingTarget_p->pinged = true;
}
void displayPing()
{
  if (haltDisplayUpdate)
    return;
  M5.Lcd.setCursor(0, 80, 2);
  if (!wifiConnected)
  {
    M5.Lcd.println("No Wifi");
  }
  else
  {
    PingTarget* pingTarget_p = pingTargetArray[pingTargetNum];
    if (pingTarget_p->pinged)
    {
      // only print ping result if we've actually pinged the host
      M5.Lcd.printf("%s: %s", pingTarget_p->displayHostname.c_str(), pingTarget_p->pingOK?"OK":"FAIL");
    }
  }
}

void secondsUpdate()
{
  bool rv;
  // Check if we're connected to Wifi yet
  if (!wifiSetupComplete)
  {
    wifiSetupComplete = connectWifi();
  }

  if (poweroffTimer > 0)
  {
    --poweroffTimer;
  }
  else
  {
    // power off after timeout
    const int cancelDelaySec = 5;
    haltDisplayUpdate = true;
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 2);
    M5.Lcd.println("Shutting Down!!!\nPress button\nto cancel");

    for (int i=0; i<(cancelDelaySec * 10); i++)
    {
      delay(100);
      M5.update();
      if (M5.BtnA.wasReleased())
      {
        displayStatus("cancelled");
        delay(2000);
        poweroffTimer = longPoweroffTime;
        haltDisplayUpdate = false;
        return;
      }
    }
    Serial.println("Shutdown after timeout");
    M5.Axp.PowerOff();
    // never reached
  }

  // Ping hosts and display live-ness
  // Note: Ping has to come before UDP send/receive or ESP32 crashes
  if (wifiSetupComplete)
  {
    // pick the next host and ping it every few sec
    if (!(poweroffTimer%4))
    {
      doPing();
      if (++pingTargetNum == pingTargetArrayLen)
        pingTargetNum = 0;
    }
    displayPing();
  }

  if (buttonA)  // Linky power toggle button
  {
    udp.writeTo((uint8_t*)"toggle", 6, linkyM5IP, udpPort);
    buttonA = false;
    Serial.println("ButtonA toggled power");
  }

  if (wifiSetupComplete)
  {
    // Serial.println("Writing status request to linkyM5");
    udp.writeTo((uint8_t*)"status", 6, linkyM5IP, udpPort);
  }

}

void setup() {
  // initialize M5StickC
  M5.begin();
  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
  displayStatus("starting");
  M5.update();

  Serial.begin(115200);

  // Set WiFi to station mode and disconnect from an AP if it was previously connected.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

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
