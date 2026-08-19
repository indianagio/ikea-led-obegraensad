#include "plugins/PongPlugin.h"

#include "pixelfx.h"

#include <math.h>

namespace
{
constexpr unsigned long STEP_MS = 33;
constexpr unsigned long SERVE_PAUSE_MS = 1200;
constexpr float PAD_EASE = 0.35f;   // how sharply a paddle follows the finger
constexpr float AI_EASE = 0.16f;    // the AI is deliberately softer than a human
constexpr int LEFT = 0;
constexpr int RIGHT = 1;

float clampf(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

void PongPlugin::serve(int8_t direction)
{
  this->ballX = 8.0f;
  this->ballY = (float)random(4, 12);
  this->serveDirection = direction;

  const float speed = 0.30f * ((float)this->paramValue("ballspeed") / 100.0f);
  this->ballVX = speed * (float)direction;
  this->ballVY = speed * ((float)random(-70, 71) / 100.0f);

  this->serveAt = millis() + SERVE_PAUSE_MS;
}

void PongPlugin::resetMatch()
{
  this->score[LEFT] = 0;
  this->score[RIGHT] = 0;
  this->displayScore[LEFT] = 0;
  this->displayScore[RIGHT] = 0;
  this->serve(random(2) ? 1 : -1);
  this->broadcastGame();
}

void PongPlugin::scorePoint(int side)
{
  this->score[side]++;
  this->displayScore[LEFT] = this->score[LEFT];
  this->displayScore[RIGHT] = this->score[RIGHT];
  this->showScore = true;

  const int target = this->paramValue("target");
  if (this->score[side] >= target)
  {
    this->serve(side == LEFT ? 1 : -1);
    this->serveAt = millis() + SERVE_PAUSE_MS * 2; // let the winning score sit
    // displayScore keeps the winning figure on the panel while the live score
    // resets for the next match.
    this->score[LEFT] = 0;
    this->score[RIGHT] = 0;
    this->broadcastGame();
    return;
  }

  // Serve towards whoever just conceded.
  this->serve(side == LEFT ? -1 : 1);
  this->broadcastGame();
}

void PongPlugin::updateAI(int side)
{
  // Track the ball, but only once it is heading this way, and with a bias error
  // so it stays beatable.
  const bool incoming = (side == LEFT) ? this->ballVX < 0.0f : this->ballVX > 0.0f;
  if (!incoming)
  {
    this->padTarget[side] = 7.5f;
    return;
  }

  const float error = (float)((int)(this->ballX * 7.0f) % 5) - 2.0f;
  this->padTarget[side] = clampf(this->ballY + error * 0.35f, 0.0f, (float)(ROWS - 1));
}

void PongPlugin::updatePaddles(float dt)
{
  const float half = (float)this->paramValue("paddle") / 2.0f;

  for (int side = 0; side < 2; side++)
  {
    if (this->isSeatHeld(side))
      this->padTarget[side] = this->seatPosition(side) * (float)(ROWS - 1);
    else
      this->updateAI(side);

    // Framerate-independent smoothing: the same pull whatever the frame took.
    const float ease = this->isSeatHeld(side) ? PAD_EASE : AI_EASE;
    const float k = 1.0f - powf(1.0f - ease, dt);
    this->padY[side] += (this->padTarget[side] - this->padY[side]) * k;
    this->padY[side] = clampf(this->padY[side], half - 0.5f, (float)(ROWS - 1) - half + 0.5f);
  }
}

void PongPlugin::updateBall(float dt)
{
  if (millis() < this->serveAt)
    return;

  this->showScore = false;

  this->ballX += this->ballVX * dt;
  this->ballY += this->ballVY * dt;

  if (this->ballY < 0.0f)
  {
    this->ballY = -this->ballY;
    this->ballVY = -this->ballVY;
  }
  else if (this->ballY > (float)(ROWS - 1))
  {
    this->ballY = 2.0f * (float)(ROWS - 1) - this->ballY;
    this->ballVY = -this->ballVY;
  }

  const float half = (float)this->paramValue("paddle") / 2.0f;

  if (this->ballVX < 0.0f && this->ballX <= 1.0f)
  {
    if (fabsf(this->ballY - this->padY[LEFT]) <= half + 0.5f)
    {
      this->ballX = 1.0f;
      this->ballVX = -this->ballVX;
      // Hitting away from the centre puts spin on it.
      this->ballVY += (this->ballY - this->padY[LEFT]) / half * 0.16f;
    }
    else if (this->ballX < 0.0f)
    {
      this->scorePoint(RIGHT);
      return;
    }
  }
  else if (this->ballVX > 0.0f && this->ballX >= (float)(COLS - 2))
  {
    if (fabsf(this->ballY - this->padY[RIGHT]) <= half + 0.5f)
    {
      this->ballX = (float)(COLS - 2);
      this->ballVX = -this->ballVX;
      this->ballVY += (this->ballY - this->padY[RIGHT]) / half * 0.16f;
    }
    else if (this->ballX > (float)(COLS - 1))
    {
      this->scorePoint(LEFT);
      return;
    }
  }

  this->ballVY = clampf(this->ballVY, -0.55f, 0.55f);
}

void PongPlugin::render()
{
  Screen.clear();

  // Dim net down the middle.
  for (int y = 0; y < ROWS; y += 2)
    Screen.setPixel(8, y, 1, 18);

  const int size = this->paramValue("paddle");
  for (int side = 0; side < 2; side++)
  {
    const int x = (side == LEFT) ? 0 : COLS - 1;
    // A seated player gets a brighter paddle than the AI.
    const uint8_t shade = this->isSeatHeld(side) ? 255 : 130;
    const float top = this->padY[side] - (float)size / 2.0f + 0.5f;
    pixelfx::barV(x, top, top + (float)size, shade);
  }

  if (this->showScore && millis() < this->serveAt)
  {
    std::vector<int> left = {this->displayScore[LEFT] % 10};
    std::vector<int> right = {this->displayScore[RIGHT] % 10};
    Screen.drawNumbers(3, 6, left, MAX_BRIGHTNESS);
    Screen.drawNumbers(10, 6, right, MAX_BRIGHTNESS);
    return;
  }

  pixelfx::dot(this->ballX, this->ballY, MAX_BRIGHTNESS);
}

void PongPlugin::setup()
{
  this->useSpeed();
  this->addParam("paddle", "Paddle size", 2, 6, 4);
  this->addParam("ballspeed", "Ball speed", 40, 220, 100);
  this->addParam("target", "Points to win", 3, 15, 5);

  this->useSeats(2);
  for (int i = 0; i < 2; i++)
  {
    this->padY[i] = 7.5f;
    this->padTarget[i] = 7.5f;
  }

  this->lastStepAt = 0;
  this->resetMatch();
  Screen.clear();
}

void PongPlugin::loop()
{
  if (!timer.isReady(this->scaled(STEP_MS)))
    return;

  const unsigned long now = millis();
  float dt = this->lastStepAt ? (float)(now - this->lastStepAt) / (float)STEP_MS : 1.0f;
  this->lastStepAt = now;
  if (dt > 3.0f) // a long stall must not teleport the ball through a paddle
    dt = 3.0f;

  this->updateSeats();
  this->updatePaddles(dt);
  this->updateBall(dt);
  this->render();
}

String PongPlugin::getStatus() const
{
  String status = String(this->displayScore[LEFT]);
  status += " : ";
  status += this->displayScore[RIGHT];
  return status;
}

const char *PongPlugin::getName() const
{
  return "Pong";
}
