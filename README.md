# StarlinkFob

The Starlink Fob is a handheld device for toggling power
to the Starlink antenna, and for displaying battery status. The code runs on an M5StickCPlus that's on the
RV wifi network. See the design documentation for a description of the system, and
Starlink Fob's place in it.

## Design Documentation

System design documentation and requirements are at:
https://sites.google.com/site/paulbouchier/home/projects/starlink

## Wifi passwords

The software has a menuing system which lets you select an SSID to connect to, and allows setting the
wifi password in the manner of setting date/time on a watch.

## Build

### Prerequisites

You must have the M5Stack board support installed in your Arduino environment - follow
these instructions: https://docs.m5stack.com/en/arduino/arduino_ide

Install required libraries using these instructions: https://roboticsbackend.com/install-arduino-library-from-github/

Required libraries:
- ESP32Ping library from  https://github.com/marian-craciunescu/ESP32Ping
- M5StickCPlus from https://github.com/m5stack/M5StickC-Plus
- M5StickCPlus2 from https://github.com/m5stack/M5StickCPlus2

