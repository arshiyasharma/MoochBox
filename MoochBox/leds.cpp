#include <Arduino.h>
#include "leds.h"

static const int GREEN_LED = 18;
static const int RED_LED = 19;

static LedState current = LED_CONNECTING;
static unsigned long flashStart = 0;
static bool flashing = false;

void ledsBegin() {
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
}

void ledsSet(LedState state) { current = state; }

void ledsFlashAdded() {
  flashing = true;
  flashStart = millis();
}

void ledsUpdate() {
  unsigned long now = millis();
  bool blinkOn = (now / 400) % 2 == 0;
  bool green = false;
  bool red = false;

  switch (current) {
    case LED_CONNECTING: green = blinkOn; break;
    case LED_OK:         green = true; break;
    case LED_ERROR:      red = true; break;
    case LED_NO_DEVICE:  red = blinkOn; break;
  }

  // Double flash: off-on-off-on over 600 ms, then back to the normal pattern.
  if (flashing) {
    unsigned long t = now - flashStart;
    if (t < 600) {
      green = (t / 150) % 2 == 1;
      red = false;
    } else {
      flashing = false;
    }
  }

  digitalWrite(GREEN_LED, green ? HIGH : LOW);
  digitalWrite(RED_LED, red ? HIGH : LOW);
}
