#pragma once

#include "PluginManager.h"
#include "timing.h"

class CandlePlugin : public Plugin
{
private:
  NonBlockingDelay timer;

  float phase = 0.0f;       // animation clock, drives the noise lookups
  float sway = 0.0f;        // lateral lean of the flame axis
  float swayTarget = 0.0f;
  float height = 11.0f;     // flame height in pixels
  float heightTarget = 11.0f;
  float gain = 1.0f;        // global brightness flicker
  float gainTarget = 1.0f;
  uint16_t gustTicks = 0;   // frames left in a draft event

  // Cached from the params, so the hot loop never walks the param list.
  uint8_t heightPct = 100;
  uint8_t swayPct = 100;
  uint8_t flickerPct = 100;
  uint8_t widthPct = 100;

  static float hash(uint32_t a, uint32_t b);
  float noise(int row, float t) const;

  void syncParams();
  void updateMotion();
  void drawFlame();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
  void onParamChanged(const char *key, int value) override;
};
