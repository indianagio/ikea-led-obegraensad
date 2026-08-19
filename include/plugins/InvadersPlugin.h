#pragma once

#include "PluginManager.h"
#include "timing.h"

class InvadersPlugin : public Plugin
{
private:
  static constexpr uint8_t COLUMNS = 6;   // invaders across
  static constexpr uint8_t RANKS = 3;     // invaders down
  static constexpr uint8_t STRIDE = 2;    // pixels between invaders
  static constexpr uint8_t MAX_BOMBS = 3;
  static constexpr uint8_t SHIP_ROW = ROWS - 1;

  NonBlockingDelay timer;
  unsigned long lastStepAt = 0;

  bool alive[RANKS * COLUMNS] = {false};
  uint8_t remaining = 0;
  float fleetX = 2.0f;     // left edge of the formation
  float fleetY = 1.0f;     // top row of the formation
  int8_t fleetDir = 1;
  unsigned long nextMarchAt = 0;

  float shipX = 8.0f;
  bool shotActive = false;
  float shotX = 0.0f, shotY = 0.0f;

  struct Bomb
  {
    float x, y;
    bool alive;
  };
  Bomb bombs[MAX_BOMBS] = {};
  unsigned long nextBombAt = 0;

  uint8_t lives = 3;
  uint8_t wave = 1;
  uint16_t score = 0;
  unsigned long holdUntil = 0;
  bool gameOver = false;

  float invaderX(uint8_t col) const;
  float invaderY(uint8_t rank) const;
  void buildWave(bool firstWave);
  void marchFleet();
  void fire();
  void dropBomb();
  void loseLife();
  void updateShip(float dt);
  void updateShots(float dt);
  void render();

public:
  void setup() override;
  void loop() override;
  const char *getName() const override;
  String getStatus() const override;
  char getAxis() const override;
  bool hasButton() const override;
};
