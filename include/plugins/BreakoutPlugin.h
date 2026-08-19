#pragma once

#include "PluginManager.h"
#include "timing.h"

class BreakoutPlugin : public Plugin
{
private:
  static constexpr uint8_t BRICK_ROWS = 4;
  static constexpr uint8_t PADDLE_ROW = ROWS - 1;

  NonBlockingDelay timer;
  unsigned long lastStepAt = 0; // for delta-time integration

  bool bricks[BRICK_ROWS * COLS] = {false};
  uint8_t remaining = 0;

  float paddleX = 8.0f;       // paddle centre
  float paddleTarget = 8.0f;
  int8_t aiDirection = 1;     // used when nobody is playing

  float ballX = 8.0f, ballY = 13.0f;
  float ballVX = 0.0f, ballVY = 0.0f;

  uint8_t lives = 3;
  uint8_t level = 1;
  uint16_t score = 0;

  unsigned long holdUntil = 0; // score/gameover screen
  bool gameOver = false;

  float ballSpeed() const;
  void buildLevel(bool firstLevel);
  void launchBall();
  void loseLife();
  void updatePaddle(float dt);
  void updateBall(float dt);
  void render();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
  String getStatus() const override;
  char getAxis() const override;
};
