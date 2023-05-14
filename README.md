# StarlinkFob

The Starlink Fob is a handheld device for toggling power
to the Starlink antenna, and for displaying battery status. The code runs on an M5StickCPlus that's on the
RV wifi network. See the design documentation for a description of the system, and
Starlink Fob's place in it.

## Design Documentation

System design documentation and requirements are at:
https://sites.google.com/site/paulbouchier/home/projects/starlink

## Wifi passwords

This repo contains a file: secrets.template.h. Follow the instructions in that file to set
your SSID and Wifi password, and rename it to secrets.h to enable building the project in
the Arduino environment.

## Build

### Prerequisites

You must have the following libraries installed in your Arduino environment:
- M5Stack board support
- 
