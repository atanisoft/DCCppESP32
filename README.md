# What is ESP32 Command Station
ESP32 Command Station is an open-source hardware and software OpenLCB enabled Command Station for the operation of DCC decoder equipped model railroads.

The ESP32 Command Station consists of an ESP32 module with up to two h-bridge devices to generate the DCC signal for the tracks. Note that only one h-bridge will be enabled at any given time, it is not possible to use both OPS and PROG tracks simultaneously. It is also possible to disable either the OPS or PROG track output entirely via configuration settings.

Support for the following add-ons is available:
1. CAN transceiver (MCP2551 or SN65HVD23X) for LCC CAN connectivity.
2. OLED or LCD display for command station status.
3. Addressable RGB LEDs for visual status indicators.

Full documentation can be found [here](https://atanisoft.github.io/ESP32CommandStation/).

[![Build Status](https://github.com/atanisoft/ESP32CommandStation/workflows/Build/badge.svg)](https://github.com/atanisoft/ESP32CommandStation/actions)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/c29397e7912b4def98a7c04317258588)](https://www.codacy.com/gh/atanisoft/ESP32CommandStation/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=atanisoft/ESP32CommandStation&amp;utm_campaign=Badge_Grade)
[![Contributors](https://img.shields.io/github/contributors/atanisoft/ESP32CommandStation.svg)](https://github.com/atanisoft/ESP32CommandStation/graphs/contributors)
[![Stars](https://img.shields.io/github/stars/atanisoft/ESP32CommandStation.svg)](https://github.com/atanisoft/ESP32CommandStation/stargazers)
[![License](https://img.shields.io/github/license/atanisoft/ESP32CommandStation.svg)](https://github.com/atanisoft/ESP32CommandStation/blob/master/LICENSE)

-May 01, 2020
