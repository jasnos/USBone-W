#pragma once
#include "Arduino.h"

// Waveshare ESP32-S3-LCD-1.47 uses pin 38 for RGB LED
#ifdef PIN_NEOPIXEL
#undef PIN_NEOPIXEL
#endif
#define PIN_NEOPIXEL 38

void Set_Color(uint8_t Red,uint8_t Green,uint8_t Blue);                 // Set RGB bead color
void RGB_Lamp_Loop(uint16_t Waiting);                                   // The lamp beads change color in cycles