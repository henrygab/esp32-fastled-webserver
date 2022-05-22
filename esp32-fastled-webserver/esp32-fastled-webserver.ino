/*
   ESP32 FastLED WebServer: https://github.com/jasoncoon/esp32-fastled-webserver
   Copyright (C) 2017 Jason Coon

   Built upon the amazing FastLED work of Daniel Garcia and Mark Kriegsman:
   https://github.com/FastLED/FastLED

   ESP32 support provided by the hard work of Sam Guyer:
   https://github.com/samguyer/FastLED

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include <HTTPUpdateServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ESPmDNS.h>

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3003000)
#warning "Requires FastLED 3.3 or later; check github for latest code."
#endif

WebServer webServer(80);
HTTPUpdateServer httpUpdateServer;
WiFiManager wifiManager;

#define NAME_PREFIX "Fibonacci1024-"
String nameString;

const int led = 5;

uint8_t autoplay = 0;
uint8_t autoplayDuration = 10;
unsigned long autoPlayTimeout = 0;

uint8_t currentPatternIndex = 0; // Index number of which pattern is current

uint8_t gHue = 0; // rotating "base color" used by many of the patterns

uint8_t power = 1;
uint8_t brightness = 8;

uint8_t speed = 16;

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100
uint8_t cooling = 50;

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
uint8_t sparking = 120;

CRGB solidColor = CRGB::Blue;

uint8_t cyclePalettes = 0;
uint8_t paletteDuration = 10;
uint8_t currentPaletteIndex = 0;
unsigned long paletteTimeout = 0;

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define DATA_PIN    18
#define LED_TYPE    SK6812
#define COLOR_ORDER GRB
#define NUM_LEDS 1024
CRGB leds[NUM_LEDS];

#define MILLI_AMPS         10000 // IMPORTANT: set the max milli-Amps of your power supply (4A = 4000mA)
#define FRAMES_PER_SECOND  120

#include "palettes.h"
#include "map.h"

#include "noise.h"
#include "patterns.h"

#include "field.h"
#include "fields.h"

#include "static_eval.h"
#include "constexpr_strlen.h"
#include "web.h"

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void setup() {
  pinMode(led, OUTPUT);
  digitalWrite(led, 1);

  // delay(3000); // 3 second delay for recovery
  Serial.begin(9600);

  SPIFFS.begin();
  listDir(SPIFFS, "/", 1);

  loadFieldsFromEEPROM(fields, fieldCount);

  setupWeb();

  FastLED.addLeds<LED_TYPE, 16, COLOR_ORDER>(leds, 0 * 205, 205); // LED1 on QuinLED-Dig-Quad
  FastLED.addLeds<LED_TYPE,  3, COLOR_ORDER>(leds, 1 * 205, 205); // LED2
  FastLED.addLeds<LED_TYPE,  1, COLOR_ORDER>(leds, 2 * 205, 205); // LED3
  FastLED.addLeds<LED_TYPE,  4, COLOR_ORDER>(leds, 3 * 205, 205); // LED4
  FastLED.addLeds<LED_TYPE, 15, COLOR_ORDER>(leds, 4 * 205, 204); // Q1R

  // FastLED.setDither(BINARY_DITHER);
  FastLED.setDither(DISABLE_DITHER);
  
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  
  // set master brightness control
  FastLED.setBrightness(brightness);

  loadFieldsFromEEPROM(fields, fieldCount);

  autoPlayTimeout = millis() + (autoplayDuration * 1000);
}

void loop()
{
  handleWeb();

  if (power == 0) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  }
  else {
    // Call the current pattern function once, updating the 'leds' array
    patterns[currentPatternIndex].pattern();

    EVERY_N_MILLISECONDS(40) {
      // slowly blend the current palette to the next
      nblendPaletteTowardPalette(currentPalette, targetPalette, 8);
      gHue++;  // slowly cycle the "base color" through the rainbow
    }

    if (autoplay == 1 && (millis() > autoPlayTimeout)) {
      nextPattern();
      autoPlayTimeout = millis() + (autoplayDuration * 1000);
    }

    if (cyclePalettes == 1 && (millis() > paletteTimeout)) {
      nextPalette();
      paletteTimeout = millis() + (paletteDuration * 1000);
    }
  }

  // send the 'leds' array out to the actual LED strip
  // insert a delay to keep the framerate modest
  FastLED.delay(1000 / FRAMES_PER_SECOND);
  // delay(1000 / FRAMES_PER_SECOND);
}

void nextPattern()
{
  // add one to the current pattern number, and wrap around at the end
  currentPatternIndex = (currentPatternIndex + 1) % patternCount;
}

void nextPalette()
{
  currentPaletteIndex = (currentPaletteIndex + 1) % paletteCount;
  targetPalette = palettes[currentPaletteIndex];
}
