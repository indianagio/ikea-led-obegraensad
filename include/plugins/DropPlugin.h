#pragma once

#include "PluginManager.h"
#include "timing.h"

class DropPlugin : public Plugin
{
private:
  NonBlockingDelay timer;

  // Surface state: one displacement and one velocity per column.
  float h[COLS] = {0.0f};
  float v[COLS] = {0.0f};

  // The falling droplet.
  bool dropActive = false;
  float dropX = 0.0f;
  float dropY = 0.0f;
  float dropVel = 0.0f;
  unsigned long nextDropAt = 0;

  // Splash thrown up by the impact.
  struct Particle
  {
    float x, y, vx, vy;
    bool alive;
  };
  Particle particles[6] = {};

  float baseline() const;
  float surfaceAt(int x) const;
  void disturb(int col, float strength);
  void releaseDrop();
  void updateWaves();
  void updateDrop();
  void updateParticles();
  void render();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
};
