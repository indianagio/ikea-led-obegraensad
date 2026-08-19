#pragma once

#include "PluginManager.h"
#include "timing.h"

class PongPlugin : public Plugin
{
private:
  NonBlockingDelay timer;
  unsigned long lastStepAt = 0; // for delta-time integration

  float ballX = 8.0f, ballY = 8.0f;
  float ballVX = 0.0f, ballVY = 0.0f;

  float padY[2] = {7.5f, 7.5f};       // paddle centres, in rows
  float padTarget[2] = {7.5f, 7.5f};  // where the seat holder wants it

  uint8_t score[2] = {0, 0};
  uint8_t displayScore[2] = {0, 0}; // frozen while the score is on screen
  unsigned long serveAt = 0;      // ball is frozen until this moment
  int8_t serveDirection = 1;
  bool showScore = false;

  void resetMatch();
  void serve(int8_t direction);
  void scorePoint(int side);
  void updateAI(int side);
  void updatePaddles(float dt);
  void updateBall(float dt);
  void render();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
  String getStatus() const override;
};
