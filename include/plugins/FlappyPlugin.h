#pragma once

#include "PluginManager.h"
#include "timing.h"

class FlappyPlugin : public Plugin
{
private:
  static constexpr uint8_t PIPE_COUNT = 2;
  static constexpr float BIRD_X = 4.0f;

  NonBlockingDelay timer;
  unsigned long lastStepAt = 0;

  float birdY = 8.0f;
  float birdVY = 0.0f;

  struct Pipe
  {
    float x;      // column of the pipe, fractional so it scrolls smoothly
    int8_t gapTop; // first open row
    bool counted;  // already scored
  };
  Pipe pipes[PIPE_COUNT] = {};

  uint16_t score = 0;
  uint16_t best = 0;
  unsigned long holdUntil = 0;
  bool dead = false;

  int gapSize() const;
  void reset();
  void placePipe(uint8_t index, float x);
  void die();
  void updateBird(float dt);
  void updatePipes(float dt);
  void render();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
  String getStatus() const override;
  char getAxis() const override;
  bool hasButton() const override;
};
