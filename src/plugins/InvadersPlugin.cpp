#include "plugins/InvadersPlugin.h"

#include "pixelfx.h"

#include <math.h>

namespace
{
constexpr unsigned long STEP_MS = 33;
constexpr unsigned long HOLD_MS = 1600;
constexpr float SHIP_EASE = 0.4f;
constexpr float SHOT_SPEED = 0.55f;
constexpr float BOMB_SPEED = 0.16f;
constexpr uint8_t SHIP_WIDTH = 3;
constexpr uint8_t INVADER_SHADE[3] = {230, 175, 130};

float clampf(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

float InvadersPlugin::invaderX(uint8_t col) const
{
  return this->fleetX + (float)(col * STRIDE);
}

float InvadersPlugin::invaderY(uint8_t rank) const
{
  return this->fleetY + (float)(rank * STRIDE);
}

void InvadersPlugin::buildWave(bool firstWave)
{
  for (uint8_t i = 0; i < RANKS * COLUMNS; i++)
    this->alive[i] = true;
  this->remaining = RANKS * COLUMNS;

  this->fleetX = 2.0f;
  this->fleetY = 1.0f;
  this->fleetDir = 1;
  this->nextMarchAt = 0;

  this->shotActive = false;
  for (auto &bomb : this->bombs)
    bomb.alive = false;

  if (firstWave)
  {
    this->wave = 1;
    this->lives = 3;
    this->score = 0;
    this->gameOver = false;
  }
}

void InvadersPlugin::marchFleet()
{
  // The fewer invaders left, the faster the rest advance.
  const float pace = (float)this->paramValue("fleetspeed") / 100.0f;
  const float pressure = 1.0f + (float)(RANKS * COLUMNS - this->remaining) * 0.06f;
  unsigned long interval = (unsigned long)(650.0f / (pace * pressure * (1.0f + 0.15f * (this->wave - 1))));
  if (interval < 60)
    interval = 60;

  this->nextMarchAt = millis() + interval;

  const float rightEdge = this->fleetX + (float)((COLUMNS - 1) * STRIDE);
  if ((this->fleetDir > 0 && rightEdge >= (float)(COLS - 1)) ||
      (this->fleetDir < 0 && this->fleetX <= 0.0f))
  {
    this->fleetDir = -this->fleetDir;
    this->fleetY += 1.0f;

    if (this->invaderY(RANKS - 1) >= (float)(SHIP_ROW - 1))
    {
      this->lives = 1; // they have landed: this is the last life whatever is left
      this->loseLife();
    }
    return;
  }

  this->fleetX += (float)this->fleetDir;
}

void InvadersPlugin::fire()
{
  if (this->shotActive)
    return; // one shot in flight, as the original

  this->shotActive = true;
  this->shotX = this->shipX;
  this->shotY = (float)SHIP_ROW - 1.0f;
}

void InvadersPlugin::dropBomb()
{
  // Only an invader with nothing below it in its column may drop.
  uint8_t candidates[COLUMNS];
  uint8_t ranks[COLUMNS];
  uint8_t count = 0;

  for (uint8_t col = 0; col < COLUMNS; col++)
  {
    for (int8_t rank = RANKS - 1; rank >= 0; rank--)
    {
      if (this->alive[rank * COLUMNS + col])
      {
        candidates[count] = col;
        ranks[count] = (uint8_t)rank;
        count++;
        break;
      }
    }
  }

  if (count == 0)
    return;

  const uint8_t pick = (uint8_t)random(0, count);

  for (auto &bomb : this->bombs)
  {
    if (bomb.alive)
      continue;

    bomb.x = this->invaderX(candidates[pick]);
    bomb.y = this->invaderY(ranks[pick]) + 1.0f;
    bomb.alive = true;
    return;
  }
}

void InvadersPlugin::loseLife()
{
  this->holdUntil = millis() + HOLD_MS;

  if (this->lives > 0)
    this->lives--;

  this->shotActive = false;
  for (auto &bomb : this->bombs)
    bomb.alive = false;

  if (this->lives == 0)
    this->gameOver = true;
}

void InvadersPlugin::updateShip(float dt)
{
  const float half = (float)SHIP_WIDTH / 2.0f;
  float target;

  if (this->isSeatHeld(0))
  {
    target = this->seatPosition(0) * (float)(COLS - 1);
    if (this->takePress(0))
      this->fire();
  }
  else
  {
    // The board plays: line up under the lowest invader still standing and shoot.
    target = this->shipX;
    for (int8_t rank = RANKS - 1; rank >= 0 && target == this->shipX; rank--)
      for (uint8_t col = 0; col < COLUMNS; col++)
        if (this->alive[rank * COLUMNS + col])
        {
          target = this->invaderX(col);
          break;
        }

    if (fabsf(target - this->shipX) < 0.6f)
      this->fire();
  }

  target = clampf(target, half - 0.5f, (float)(COLS - 1) - half + 0.5f);
  const float k = 1.0f - powf(1.0f - SHIP_EASE, dt);
  this->shipX += (target - this->shipX) * k;
}

void InvadersPlugin::updateShots(float dt)
{
  if (this->shotActive)
  {
    this->shotY -= SHOT_SPEED * dt;

    if (this->shotY < 0.0f)
    {
      this->shotActive = false;
    }
    else
    {
      for (uint8_t rank = 0; rank < RANKS && this->shotActive; rank++)
      {
        for (uint8_t col = 0; col < COLUMNS; col++)
        {
          const uint8_t index = rank * COLUMNS + col;
          if (!this->alive[index])
            continue;

          if (fabsf(this->shotX - this->invaderX(col)) < 0.9f &&
              fabsf(this->shotY - this->invaderY(rank)) < 0.9f)
          {
            this->alive[index] = false;
            this->remaining--;
            this->score += (uint16_t)(RANKS - rank); // the back rows are worth more
            this->shotActive = false;
            break;
          }
        }
      }
    }
  }

  if (this->remaining == 0)
  {
    this->wave++;
    this->holdUntil = millis() + HOLD_MS;
    this->buildWave(false);
    return;
  }

  if (millis() >= this->nextBombAt)
  {
    const unsigned long rate = (unsigned long)this->paramValue("bombrate") * 100ul;
    this->nextBombAt = millis() + rate / 2 + (unsigned long)random(0, (long)rate + 1);
    this->dropBomb();
  }

  for (auto &bomb : this->bombs)
  {
    if (!bomb.alive)
      continue;

    bomb.y += BOMB_SPEED * dt;

    if (bomb.y > (float)SHIP_ROW)
    {
      bomb.alive = false;
      continue;
    }

    if (bomb.y >= (float)SHIP_ROW - 0.5f &&
        fabsf(bomb.x - this->shipX) <= (float)SHIP_WIDTH / 2.0f)
    {
      bomb.alive = false;
      this->loseLife();
      return;
    }
  }
}

void InvadersPlugin::render()
{
  Screen.clear();

  for (uint8_t rank = 0; rank < RANKS; rank++)
    for (uint8_t col = 0; col < COLUMNS; col++)
      if (this->alive[rank * COLUMNS + col])
        pixelfx::dot(this->invaderX(col), this->invaderY(rank), INVADER_SHADE[rank]);

  const float left = this->shipX - (float)SHIP_WIDTH / 2.0f + 0.5f;
  pixelfx::barH(SHIP_ROW, left, left + (float)SHIP_WIDTH,
                this->isSeatHeld(0) ? 255 : 150);

  if (this->shotActive)
    pixelfx::dot(this->shotX, this->shotY, MAX_BRIGHTNESS);

  for (const auto &bomb : this->bombs)
    if (bomb.alive)
      pixelfx::dot(bomb.x, bomb.y, 190);

  if (millis() < this->holdUntil)
  {
    std::vector<int> digits;
    if (this->score >= 10)
      digits.push_back((this->score / 10) % 10);
    digits.push_back(this->score % 10);
    Screen.drawNumbers(digits.size() > 1 ? 3 : 6, 4, digits, MAX_BRIGHTNESS);

    const int shown = this->lives > 3 ? 3 : this->lives;
    const int startX = 8 - (shown * 2 - 1) / 2 - 1;
    for (int i = 0; i < shown; i++)
      Screen.setPixel(startX + i * 2, 12, 1, 160);
  }
}

void InvadersPlugin::setup()
{
  this->useSpeed();
  this->useSeats(1);
  this->addParam("fleetspeed", "Fleet speed", 40, 200, 100);
  this->addParam("bombrate", "Bombs every (0.1s)", 5, 60, 18);

  this->lastStepAt = 0;
  this->holdUntil = 0;
  this->shipX = 8.0f;
  this->buildWave(true);

  Screen.clear();
}

void InvadersPlugin::loop()
{
  if (!timer.isReady(this->scaled(STEP_MS)))
    return;

  const unsigned long now = millis();
  float dt = this->lastStepAt ? (float)(now - this->lastStepAt) / (float)STEP_MS : 1.0f;
  this->lastStepAt = now;
  if (dt > 3.0f)
    dt = 3.0f;

  this->updateSeats();

  if (now < this->holdUntil)
  {
    this->render();
    return;
  }

  if (this->gameOver)
  {
    this->buildWave(true);
    this->render();
    return;
  }

  if (now >= this->nextMarchAt)
    this->marchFleet();

  this->updateShip(dt);
  this->updateShots(dt);
  this->render();
}

String InvadersPlugin::getStatus() const
{
  String status = "Score ";
  status += this->score;
  status += "  Wave ";
  status += this->wave;
  status += "  ";
  status += this->lives;
  status += this->lives == 1 ? " life" : " lives";
  return status;
}

char InvadersPlugin::getAxis() const
{
  return 'x';
}

bool InvadersPlugin::hasButton() const
{
  return true;
}

const char *InvadersPlugin::getName() const
{
  return "Invaders";
}
