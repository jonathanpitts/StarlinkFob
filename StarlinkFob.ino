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
int displayMode = 0;  // 0: voltage, ping, 1: SSID
char statusMsg[50];
bool haltDisplayUpdate = false;

// Time-related variables
int64_t secondsSinceStart = 0;
int64_t nextSecondTime;
const int initialPoweroffTime = 120;
const int longPoweroffTime = 300;
int poweroffTimer = initialPoweroffTime;
const int cancelDelaySec = 60;
int cancelTimer = cancelDelaySec;
const int pingPeriod = 4;

// Network variables
bool wifiScanned = false;
bool wifiConnecting = false;
bool wifiConnected = false;
bool udpListening = false;
bool wifiSetupComplete = false;
const int maxSSIDLen = 65;
char configuredSSID[maxSSIDLen];
char configuredSSIDPwd[maxSSIDLen];
const int udpPort = 6970;

IPAddress linkyM5IP(192, 168, 8, 10);
IPAddress linkyIP(192, 168, 100, 1);
IPAddress linkyRouterIP(192, 168, 8, 1);
IPAddress rvRouterIP(192, 168, 12, 1);
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
PingTarget rvRouter = {.displayHostname="rvRouter", .useIP=true, .pingIP=rvRouterIP, .pinged=false, .pingOK=false};
PingTarget linkyRouter = {.displayHostname="linkyRouter", .useIP=true, .pingIP=linkyRouterIP, .pinged=false, .pingOK=false};
PingTarget linkyM5 = {.displayHostname="linkyM5stick", .useIP=true, .pingIP=linkyM5IP, .pinged=false, .pingOK=false};
PingTarget linky = {.displayHostname="linky", .useIP=true, .pingIP=linkyIP, .pinged=false, .pingOK=false};
PingTarget dns = {.displayHostname="DNS server", .useIP=true, .pingIP=primaryDNS, .pinged=false, .pingOK=false};
PingTarget google = {.displayHostname="google", .useIP=false, .fqn="www.google.com", .pinged=false, .pingOK=false};

// Poor design :-( Order of ping targets in array is important, coupled to skipping rvRouter in local mode
PingTarget* pingTargetArray[] = {&rvRouter, &linkyRouter, &linkyM5, &linky, &dns, &google};
int pingTargetArrayLen = sizeof(pingTargetArray) / sizeof(&linkyRouter);
int pingTargetNum = 0;

AsyncUDP udp;

// EEPROM variables
#define SSID_ADDR 0
#define EEPROM_SIZE 10  // define the size of EEPROM(Byte).

uint8_t localMode; // 0: Remote, 1: Local

bool chooseNetwork()
{
  Serial.println("Scan start");

  // WiFi.scanNetworks will return the number of networks found.
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");
  if (n == 0)
  {
    Serial.println("no networks found");
    return(false);
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
      // check if the configured network was found in the scan
      if (!strncmp(SSID.c_str(), configuredSSID, maxSSIDLen))
      {
        Serial.printf("Found configured SSID %s\n", configuredSSID);
        return true;
      }
    }
  }

  // Delete the scan result to free memory
  WiFi.scanDelete();

  return false;
}

bool connectWifi()
{
  if (!wifiScanned)
  {
    if (!chooseNetwork())
    {
      // didn't find configured network
      Serial.println("Didn't find configured network");
      return false;
    }
    else
    {
      Serial.print("connecting to network: ");
      Serial.print(configuredSSID);
      Serial.print(" with passwd: ");
      Serial.println(configuredSSIDPwd);
      wifiScanned = true;
    }
  }

  if (!wifiConnecting)
  {
    Serial.println("working on setting up networking");
    // runs once
    WiFi.begin(configuredSSID, configuredSSIDPwd);
    wifiConnecting = true;
    return false;
  }

  if (!wifiConnected)
  {
    // polled until wifi connects
    if (WiFi.status() != WL_CONNECTED)
    {
      sprintf(statusMsg, "connecting");
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
          // print each received msg for debug
          Serial.print("Received packet from ");
          Serial.print(packet.remoteIP());
          Serial.print(", Length: ");
          Serial.print(packet.length());
          Serial.print(", Data: ");
          Serial.write(packet.data(), packet.length());
          Serial.println();
        }

        // save received msg into statusMsg
        int i;
        uint8_t *msg_p = packet.data();
        for(i=0; i<packet.length(); i++)
        {
          statusMsg[i] = static_cast<char>(*msg_p++);
        }
        statusMsg[i] = '\0';
      });
      udpListening = true;
      M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    }
  }
  return true;
}

void doPing(int pingTargetId)
{
  PingTarget* pingTarget_p = pingTargetArray[pingTargetId];
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

void displayPing(int pingTargetId)
{
  if (haltDisplayUpdate || !wifiConnected)
    return;

  PingTarget* pingTarget_p = pingTargetArray[pingTargetId];

  // only print ping result if we've actually pinged the host
  if (pingTarget_p->pinged)
  {
    M5.Lcd.printf("%s: %s\n", pingTarget_p->displayHostname.c_str(), pingTarget_p->pingOK?"OK":"FAIL");
  }
}

void displayStatus()
{
  if (haltDisplayUpdate)
    return;
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.print(statusMsg);

  // display ping status of current ping target
  M5.Lcd.setCursor(0, 80, 2);
  if (wifiSetupComplete)
  {
    displayPing(pingTargetNum);
    if (buttonA)  // Linky power toggle button
    {
      udp.writeTo((uint8_t*)"toggle", 6, linkyM5IP, udpPort);
      buttonA = false;
      Serial.println("ButtonA toggled power");
    }
  }
  else
  {
    M5.Lcd.println("No Wifi");
  }
}

void displayStatus2()
{
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.println("Not Implemented");
}

void displaySsid()
{
  M5.Lcd.setCursor(0, 0, 1);
  M5.Lcd.printf("%s SSID: %s\nM5: local/remote", localMode?"Local":"Remote", configuredSSID);
  if (buttonA)
  {
    if (localMode == 0)
    {
      localMode = 1;
    }
    else
    {
      localMode = 0;
    }
    EEPROM.write(SSID_ADDR, (uint8_t) localMode);
    EEPROM.commit();
    buttonA = 0;
    Serial.printf("Toggled localMode to %d, restarting...\n", localMode);
    ESP.restart();
  }
}

// blank LCD display & hand off to display functions
void updateDisplay()
{
  M5.Lcd.fillScreen(BLACK);

  if (displayMode == 0)
    displayStatus();
  else if (displayMode == 1)
    displayStatus2();
  else if (displayMode == 2)
    displaySsid();
  else
    displayMode = 0;  // should never get here

}

void secondsUpdate()
{
  // update the LCD display once/sec
  updateDisplay();

  // Check if we're connected to Wifi yet
  if (!wifiSetupComplete)
  {
    wifiSetupComplete = connectWifi();
  }

  // do power-off timer processing
  if (poweroffTimer > 0)
  {
    --poweroffTimer;
  }
  else
  {
    // poweroffTimer has expired, do shutdown/cancel processing.
    haltDisplayUpdate = true;
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 2);
    // Display shutdown message for cancelDelaySec seconds
    M5.Lcd.println("Shutting Down!!!\nPress M5 button\nto cancel");

    // power off after cancelTimer timeout with no ButtonA pushed
    if (cancelTimer > 0)
    {
      if (buttonA)
      {
        Serial.println("shutdown cancelled");
        buttonA = false;
        poweroffTimer = longPoweroffTime;
        haltDisplayUpdate = false;
        cancelTimer = cancelDelaySec;
        return;
      }
      else
      {
        --cancelTimer;
      }
      return;
    }
    else
    {
      Serial.println("Shutdown after timeout");
      M5.Axp.PowerOff();
      // never reached
    }
  }

  // Ping hosts and record live-ness
  // Note: Ping has to come before UDP send/receive or ESP32 crashes
  if (wifiSetupComplete)
  {
    // every few sec, pick the next host and ping it
    if (!(secondsSinceStart%pingPeriod))
    {
      if (++pingTargetNum == pingTargetArrayLen)
      {
        // Skip pinging rvRouter if local mode
        // Poor design: coupled to order of ping target array
        if (localMode == 0)
          pingTargetNum = 0;
        else
          pingTargetNum = 1;
      }

      doPing(pingTargetNum);
    }
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
  M5.update();

  sprintf(statusMsg, "starting");

  Serial.begin(115200);

  // Set WiFi to station mode and disconnect from an AP if it was previously connected.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Initialize EEPROM
  while (!EEPROM.begin(EEPROM_SIZE)) {  // Request storage of SIZE size(success return)
    Serial.println("\nFailed to initialise EEPROM!");
    M5.Lcd.println("EEPROM Fail");
    delay(1000000);
  }

  // Initialize variables from EEPROM
  localMode = EEPROM.read(SSID_ADDR);
  if (localMode == 0)
  {
    strncpy(configuredSSID, WIFI_REMOTE_SSID, maxSSIDLen);
    strncpy(configuredSSIDPwd, WIFI_REMOTE_PASSWORD, maxSSIDLen);
  }
  else
  {
    strncpy(configuredSSID, WIFI_LOCAL_SSID, maxSSIDLen);
    strncpy(configuredSSIDPwd, WIFI_LOCAL_PASSWORD, maxSSIDLen);
  }

  Serial.printf("configured SSID index: %d SSID: %s\n", localMode, configuredSSID);

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

  // Small button cycles through display mode & time set fields
  if (buttonB)
  {
    if (displayMode == 0)
      displayMode = 1;
    else if (displayMode == 1)
      displayMode = 2;
    else
      displayMode = 0;
    
    buttonB = false;
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
