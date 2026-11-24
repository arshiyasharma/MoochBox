// Green + red status LEDs (plan §9). Non-blocking: call ledsUpdate() every loop.
#pragma once

enum LedState {
  LED_CONNECTING,  // green blinking
  LED_OK,          // green solid
  LED_ERROR,       // red solid: no internet / Spotify login broken
  LED_NO_DEVICE,   // red blinking: owner needs to press play in Spotify
};

void ledsBegin();
void ledsSet(LedState state);
void ledsFlashAdded();  // quick green double flash when a song is added
void ledsUpdate();
