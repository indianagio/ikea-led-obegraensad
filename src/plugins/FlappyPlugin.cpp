#include "plugins/FlappyPlugin.h"

#include "pixelfx.h"

#include <math.h>

namespace
{
constexpr unsigned long STEP_MS = 33;
constexpr unsigned long HOLD_MS = 1800;
constexpr float GRAVITY = 0.055f;
constexpr float FLAP = -0.52f;
constexpr float MAX_FALL = 0.75f;
constexpr float PIPE_SPACING = 9.0f; // columns between one pipe and the next
constexpr uint8_t PIPE_SHADE = 110;
} // namespace

int FlappyPlugin::gapSize() const
{
  return this->paramValue("gap");
}

void FlappyPlugin::placePipe(uint8_t index, float x)
{
  const int gap = this->gapSize();
  this->pipes[index].x = x;
  // Keep at least one solid row top and bottom so a pipe always reads as one.
  this->pipes[index].gapTop = (int8_t)random(1, ROWS - gap);
  this->pipes[index].counted = false;
}

void FlappyPlugin::reset()
{
  this->birdY = 8.0f;
  this->birdVY = 0.0f;
  this->score = 0;
  this->dead = false;

  for (uint8_t i = 0; i < PIPE_COUNT; i++)
    this->placePipe(i, (float)COLS + (float)i * PIPE_SPACING);
}

void FlappyPlugin::die()
{
  if (this->dead)
    return;

  this->dead = true;
  if (this->score > this->best)
    this->best = this->score;
  this->holdUntil = millis() + HOLD_MS;
}

void FlappyPlugin::updateBird(float dt)
{
  bool flap = false;

  if (this->isSeatHeld(0))
  {
    flap = this->takePress(0);
  }
  else
  {
    // Nobody playing: the board flies it, aiming for the middle of the next gap.
    float targetY = 8.0f;
    float nearest = 1e9f;
    for (uint8_t i = 0; i < PIPE_COUNT; i++)
    {
      const float d = this->pipes[i].x - BIRD_X;
      if (d > -1.0f && d < nearest)
      {
        nearest = d;
        targetY = (float)this->pipes[i].gapTop + (float)this->gapSize() / 2.0f;
      }
    }
    flap = this->birdY > targetY && this->birdVY > -0.1f;
  }

  if (flap)
    this->birdVY = FLAP;

  this->birdVY += GRAVITY * dt;
  if (this->birdVY > MAX_FALL)
    this->birdVY = MAX_FALL;

  this->birdY += this->birdVY * dt;

  // Ceiling and floor are both fatal, like the pipes.
  if (this->birdY < 0.0f || this->birdY > (float)(ROWS - 1))
    this->die();
}

void FlappyPlugin::updatePipes(float dt)
{
  const float speed = 0.11f * ((float)this->paramValue("pipespeed") / 100.0f);
  const int gap = this->gapSize();

  for (uint8_t i = 0; i < PIPE_COUNT; i++)
  {
    Pipe &pipe = this->pipes[i];
    pipe.x -= speed * dt;

    if (pipe.x < -1.0f)
    {
      // Re-place it behind the furthest pipe rather than at a fixed offset, so
      // the spacing stays even however long the game has been running.
      float furthest = pipe.x;
      for (uint8_t j = 0; j < PIPE_COUNT; j++)
        if (this->pipes[j].x > furthest)
          furthest = this->pipes[j].x;
      this->placePipe(i, furthest + PIPE_SPACING);
      continue;
    }

    if (!pipe.counted && pipe.x < BIRD_X - 0.5f)
    {
      pipe.counted = true;
      this->score++;
    }

    // Collision: the bird is one pixel wide, so only the column it overlaps.
    if (fabsf(pipe.x - BIRD_X) < 1.0f)
    {
      const int row = (int)roundf(this->birdY);
      if (row < pipe.gapTop || row >= pipe.gapTop + gap)
        this->die();
    }
  }
}

void FlappyPlugin::render()
{
  Screen.clear();

  const int gap = this->gapSize();

  for (uint8_t i = 0; i < PIPE_COUNT; i++)
  {
    const Pipe &pipe = this->pipes[i];
    if (pipe.x < -1.0f || pipe.x > (float)COLS)
      continue;

    // Sub-pixel column: a pipe between two columns lights both, in proportion.
    const int cx = (int)floorf(pipe.x);
    const float frac = pipe.x - (float)cx;

    for (int y = 0; y < ROWS; y++)
    {
      if (y >= pipe.gapTop && y < pipe.gapTop + gap)
        continue;

      pixelfx::blend(cx, y, 1.0f - frac, PIPE_SHADE);
      pixelfx::blend(cx + 1, y, frac, PIPE_SHADE);
    }
  }

  if (millis() < this->holdUntil)
  {
    std::vector<int> digits;
    if (this->score >= 10)
      digits.push_back((this->score / 10) % 10);
    digits.push_back(this->score % 10);
    Screen.drawNumbers(digits.size() > 1 ? 3 : 6, 6, digits, MAX_BRIGHTNESS);
    return;
  }

  pixelfx::dot(BIRD_X, this->birdY, MAX_BRIGHTNESS);
}

void FlappyPlugin::setup()
{
  this->useSpeed();
  this->useSeats(1);
  this->addParam("gap", "Gap size", 3, 8, 5);
  this->addParam("pipespeed", "Pipe speed", 40, 200, 100);

  this->lastStepAt = 0;
  this->holdUntil = 0;
  this->best = 0;
  this->reset();

  Screen.clear();
}

void FlappyPlugin::loop()
{
  if (!timer.isReady(this->scaled(STEP_MS)))
    return;

  const unsigned long now = millis();
  float dt = this->lastStepAt ? (float)(now - this->lastStepAt) / (float)STEP_MS : 1.0f;
  this->lastStepAt = now;
  if (dt > 3.0f)
    dt = 3.0f;

  this->updateSeats();

  if (this->dead)
  {
    if (now >= this->holdUntil)
      this->reset();

    this->render();
    return;
  }

  this->updateBird(dt);
  this->updatePipes(dt);
  this->render();
}

String FlappyPlugin::getStatus() const
{
  String status = "Score ";
  status += this->score;
  status += "  Best ";
  status += this->best;
  return status;
}

char FlappyPlugin::getAxis() const
{
  return 'b'; // nothing to slide, just the one button
}

bool FlappyPlugin::hasButton() const
{
  return true;
}

const char *FlappyPlugin::getName() const
{
  return "Flappy";
}
