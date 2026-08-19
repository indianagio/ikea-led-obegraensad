#pragma once

#include "PluginManager.h"
#include "timing.h"

class ClockXLPlugin : public Plugin
{
private:
  NonBlockingDelay timer;
  bool hasTime = false;

  void drawDigit(int x, int y, uint8_t digit, uint8_t shade);
  void drawWaiting();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
};
