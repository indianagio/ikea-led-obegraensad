#pragma once

#include "PluginManager.h"
#include "timing.h"

class SandPlugin : public Plugin
{
private:
  NonBlockingDelay timer;

  uint8_t grid[ROWS * COLS] = {0}; // 0 = empty, otherwise the settled brightness

  // The grain in flight. Position is fractional so it can accelerate.
  bool grainActive = false;
  int grainX = 0;
  int grainY = 0;
  float velocity = 0.0f; // cells per step
  float carry = 0.0f;    // sub-cell travel not yet spent

  uint16_t settled = 0;          // grains already part of the heap
  unsigned long fillStartedAt = 0;
  unsigned long nextSpawnAt = 0;
  bool full = false;
  unsigned long holdUntil = 0;

  bool occupied(int x, int y) const;
  bool stepGrain(); // one cell of travel; false once the grain has come to rest
  void scheduleNextGrain();
  void spawnGrain();
  void restart();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
};
