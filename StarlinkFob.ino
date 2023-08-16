#include <M5StickCPlus.h>
#include "secrets.h"
#include "WiFi.h"
#include "AsyncUDP.h"
#include <string>
#include <EEPROM.h>
#include <ESP32Ping.h>

// global enums
enum ButtonName {BUTTON_M5, BUTTON_B};
enum PressType {SHORT_PRESS, LONG_PRESS};

// button variables
bool buttonA = false;
bool buttonALong = false;
int64_t buttonADownTime;
bool buttonB = false;

// Display variables
int displayMode = 0;  // 0: voltage, ping, 1: SSID
char statusMsg[50];

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
  if (!wifiSetupComplete)
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
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.print(statusMsg);

  // display ping status of current ping target
  M5.Lcd.setCursor(0, 80, 2);
  if (wifiSetupComplete)
  {
    displayPing(pingTargetNum);
  }
  else
  {
    M5.Lcd.println("No Wifi");
  }
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

void secondsUpdate()
{
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

}

class WifiInitSm
{
public:
  enum WifiStateName {SCAN, CONNECTING, UDP, WIFI_DONE};  // SSID choice state names

  WifiInitSm()
  {
    currentState = SCAN;
    nextState = SCAN;
  }

  //! @return true: advance super SM, false: no state change
  bool tick()
  {
    currentState = nextState;
    Serial.printf("In WifiInitSm state %s\n", stateNamesArray[currentState]);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 2);
    M5.Lcd.printf("%s\n", stateNamesArray[currentState]);

    switch(currentState)
    {
      case SCAN:
        if (chooseNetwork())
        {
          Serial.print("connecting to network: ");
          Serial.print(configuredSSID);
          Serial.print(" with passwd: ");
          Serial.println(configuredSSIDPwd);
          WiFi.begin(configuredSSID, configuredSSIDPwd);
          nextState = CONNECTING;
        }
        else
        {
          Serial.println("Didn't find configured network");
        }
        break;
      case CONNECTING:
        // polled until wifi connects
        if (WiFi.status() != WL_CONNECTED)
        {
          break;
        }
        Serial.print("connected to wifi with IP: ");
        Serial.println(WiFi.localIP());
        nextState = UDP;
        break;
      case UDP:
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
        }
        nextState = WIFI_DONE;
        break;
      case WIFI_DONE:
        M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
        wifiSetupComplete = true;
        return true;  // advance superSm
      default:
        nextState = SCAN; // should never get here
    }
    return false;
  }

  //! @return true: advance super SM, false: no state change
  bool buttonPress(ButtonName buttonName, PressType pressType)
  {
    Serial.println("CANCELLING WIFI"); 
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 2);
    M5.Lcd.println("CANCELLING WIFI"); 
    return true; 
  } 
private:
  WifiStateName currentState;
  WifiStateName nextState;
  String stateNamesArray[4] = {"SCAN", "CONNECTING", "UDP", "WIFI_DONE"};

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

};

class ShutdownSm
{
public:
  enum ShutdownStateName {TIMING, TIMEOUT};  // SSID choice state names

  ShutdownSm()
  {
    currentState = TIMING;
    nextState = TIMING;
  }

  //! @return true: advance super SM, false: no state change
  bool buttonPress(ButtonName buttonName, PressType pressType)
  {
    switch(currentState)
    {
      case TIMING:
        Serial.println("ERROR - ShutdownSm unexpected button");
        break;
      case TIMEOUT:
        Serial.println("shutdown cancelled");
        reset();
        nextState = TIMING;
        return true;
    }
    return false;
  }

  // do power-off timer processing
  //! @return true: advance super SM, false: no state change
  bool tick()
  {
    currentState = nextState;
    switch(currentState)
    {
      case TIMING:
        if (poweroffTimer > 0)
        {
          --poweroffTimer;
        }
        else
        {
          nextState = TIMEOUT;
          return true;
        }
        break;
      case TIMEOUT:
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0, 2);
        M5.Lcd.println("Shutting Down!!!\nPress M5 button\nto cancel");
        // power off after cancelTimer timeout with no ButtonA pushed
        if (cancelTimer > 0)
        {
          --cancelTimer;
        }
        else
        {
          Serial.println("Shutdown after timeout");
          M5.Axp.PowerOff();
          // never reached
        }
        break;
    }
    return false;
  }

  void reset()
  {
    poweroffTimer = longPoweroffTime;
    cancelTimer = cancelDelaySec;
  }

private:
  ShutdownStateName currentState;
  ShutdownStateName nextState;
  String stateNamesArray[3] = {"TIMING", "TIMEOUT"};

  const int initialPoweroffTime = 120;
  int poweroffTimer = initialPoweroffTime;
  const int longPoweroffTime = 300;
  const int cancelDelaySec = 60;
  int cancelTimer = cancelDelaySec;
};

class SsidSm
{
public:
  enum SsidStateName {SCAN, CHOOSE};  // SSID choice state names

  SsidSm()
  {
    currentState = SCAN;
    nextState = SCAN;
  }

  //! @return true: advance super SM, false: no state change
  bool buttonPress(ButtonName buttonName, PressType pressType)
  {
    return false;
  }

private:
  SsidStateName currentState;
  SsidStateName nextState;
  String stateNamesArray[2] = {"SCAN", "CHOOSE"};
};

class FobSuperSm
{
public:
  enum SuperStateName {WIFI_INIT, SYS_STATUS, SHUTDOWN, SSID, PASSWD, FACTORY}; // Superstate state names

  FobSuperSm()
  {
    currentState = WIFI_INIT;
    nextState = WIFI_INIT;
  }

  void buttonPress(ButtonName buttonName, PressType pressType)
  {
    bool rv;
    Serial.printf("FobSuperSm button press name %d type %d in state %s\n"
                  , buttonName, pressType, stateNamesArray[currentState].c_str());
    switch (currentState)
    {
      case WIFI_INIT:
        rv = wifiInitSm.buttonPress(buttonName, pressType);
        if (rv)
        {
          nextState = SYS_STATUS;
          Serial.printf("FobSuperSm next state: %s\n", stateNamesArray[nextState].c_str());
        }
        break;
      case SYS_STATUS:
        rv = statusButton(buttonName, pressType);
        Serial.printf("button push in FobSuperSm returned %d\n", rv);
        if (rv)
        {
          nextState = SSID;
          Serial.printf("FobSuperSm next state: %s\n", stateNamesArray[nextState].c_str());
        }
        break;
      case SHUTDOWN:
        rv = shutdownSm.buttonPress(buttonName, pressType);
        if (rv)
        {
          nextState = SYS_STATUS;
          Serial.printf("FobSuperSm next state: %s\n", stateNamesArray[nextState].c_str());
        }
        break;
      case SSID:
          nextState = SYS_STATUS;
          Serial.printf("FobSuperSm next state: %s\n", stateNamesArray[nextState].c_str());
        break;
      default:
        nextState = SYS_STATUS;
    }
  }

  void tick()
  {
    bool rv;
    currentState = nextState;
    switch (currentState)
    {
      case WIFI_INIT:
        rv = wifiInitSm.tick();
        if (rv)
        {
          nextState = SYS_STATUS;
          Serial.printf("FobSuperSm next state: %s\n", stateNamesArray[nextState].c_str());
        }
        break;
      case SYS_STATUS:
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0, 2);

        if (wifiSetupComplete)
        {
          // Serial.println("Writing status request to linkyM5");
          udp.writeTo((uint8_t*)"status", 6, linkyM5IP, udpPort);
        }

        displayStatus();
        
        rv = shutdownSm.tick();  // run the shutdown timer in status state
        if (rv)
        {
          nextState = SHUTDOWN;
          Serial.printf("FobSuperSm next state: %s\n", stateNamesArray[nextState].c_str());
        }
        break;
      case SHUTDOWN:
        shutdownSm.tick();  // run the cancel timer
        break;
      case SSID:
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0, 2);
        M5.Lcd.println("SSID: ");
        break;
      default:
        currentState = SYS_STATUS;
    }
  }

private:
  SuperStateName currentState;
  SuperStateName nextState;
  String stateNamesArray[6] = {"WIFI_INIT", "SYS_STATUS", "SHUTDOWN", "SSID", "PASSWD", "FACTORY"};

  bool statusButton(ButtonName buttonName, PressType pressType);

  WifiInitSm wifiInitSm = WifiInitSm();
  ShutdownSm shutdownSm = ShutdownSm();
  SsidSm ssidSm = SsidSm();
};

//! @return true: advance super SM, false: no state change
bool
FobSuperSm::statusButton(ButtonName buttonName, PressType pressType)
{
  Serial.printf("statusButton button press name %d type %d\n", buttonName, pressType);
  if (BUTTON_B == buttonName)
  {
    Serial.println("Ignoring button B press");
    return false;
  }
  if (BUTTON_M5 == buttonName)
  {
    if (LONG_PRESS == pressType)
    {
      Serial.println("Toggle power button press");
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 0, 2);
      M5.Lcd.println("TOGGLING POWER");
      udp.writeTo((uint8_t*)"toggle", 6, linkyM5IP, udpPort);
      delay(1000);
      return false;
    }
    else
    {
      Serial.print("statusButton short button press in state ");
      Serial.println(stateNamesArray[currentState]);
      return true;  // advance to next state
    }
  }
}

FobSuperSm fobSuperSm = FobSuperSm();


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
  int64_t now_us;

  // Read buttons
  M5.update();

  if (M5.BtnA.wasPressed())
  {
    // Initial depression of M5 was detected
    buttonADownTime = esp_timer_get_time();
    buttonA = true;
  }

  if (M5.BtnA.isPressed() && buttonA && !buttonALong)
  {
    // M5 button was pressed and is still pressed, check downtime
    int64_t buttonADuration = esp_timer_get_time() - buttonADownTime;
    if (buttonADuration > 1000000)
    {
      Serial.printf("long buttonA detected after %d us\n", buttonADuration);
      buttonALong = true;
      fobSuperSm.buttonPress(BUTTON_M5, LONG_PRESS);
    }
  }
  if (M5.BtnA.wasReleased())
  {
    buttonA = false;
    int64_t buttonADuration = esp_timer_get_time() - buttonADownTime;
    if (buttonALong)
    {
      Serial.printf("long buttonA ended after %d us\n", buttonADuration);
    }
    else
    {
      Serial.printf("short buttonA %d us\n", buttonADuration);
      fobSuperSm.buttonPress(BUTTON_M5, SHORT_PRESS);
    }
    buttonALong = false;
  }

  if (M5.BtnB.wasReleased())
  {
    buttonB = true;
    Serial.println("buttonB");
  }

  // Check if it's time to increment the seconds-counter
  // and process everything that should be done each second
  now_us = esp_timer_get_time();
  if (now_us > nextSecondTime)
  {
    secondsSinceStart++;
    nextSecondTime += 1000000;
    // Serial.printf("%lld toggleTime: %d state %d\n", secondsSinceStart, nextFlowToggleTime, generateFlowPulses);
    secondsUpdate();
    fobSuperSm.tick();
  }
}
