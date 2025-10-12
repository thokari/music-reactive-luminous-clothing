# Music-reactive luminous clothing

This repository contains the technical components of the **music-reactive luminous clothing** project.

## Requirements

- ESP32 development board (Bluetooth + ADC)
- Relay board for 8 channels of EL wire
- EL inverter and EL wire
- Microphone module (e.g. MAX9814)
- Android phone with **Bluetooth Electronics** by Kewlsoft

## Software

`/firmware` - Arduino / ESP32 source code  
  Controls 8 channels of EL wire through a custom SSR board.  
  Includes manual gain control via Android app (see below), audio signal sampling, and Bluetooth communication.

`/app` - Panel configuration for [*Kewlsoft Bluetooth Electronics*](https://www.keuwl.com/apps/bluetoothelectronics/) (Android)  
  Defines a single panel with control elements to set the gain and mode ("(R)eactive" or "(F)ixed pattern"), and control number of wires or delay, depending on mode selection.
  Import directly into the Bluetooth Electronics app.

## Hardware
ESP32 and switchboard housings: [Onshape CAD](https://cad.onshape.com/documents/024494521b0d33fed7c6c3d4/w/9dcb6fa1bd2ba2e03fcf2a73/e/ef04a81476ce24776f6ba34d?renderMode=0&uiState=68e6cb3a9794e43e76031f91)  

Solder joint reinforcement: [Onshape CAD](https://cad.onshape.com/documents/6c9c19aba1b72c0649f08df9/w/ad78cbaf727d8cb53e10f7b2/e/3d5b6b8d3a3d566e16adb659?renderMode=0&uiState=68e6d28f3fa232eef392542c)

Custom SSR PCB: solid-state relay board for 2 kHz AC switching (on personal request, e.g. an issue in this repository)

## Operation

1. Flash the firmware to an ESP32 board.  
2. Pair with your phone and open the Bluetooth Electronics panel.  
3. Adjust gain and pattern parameters via the app while music is playing.  
4. Observe real-time visualization of the audio envelope.
