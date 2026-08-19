// Rewritten from the original demo (https://elektro.turanis.de/html/prj104/)
// into a playable game: the paddle follows a seated player, the frame no longer
// blocks the drawing task, and bricks/lives/levels are tracked properly.
#include "plugins/BreakoutPlugin.h"

#include "pixelfx.h"

#include <math.h>

namespace
{
constexpr unsigned long STEP_MS = 33;
constexpr unsigned long HOLD_MS = 1600;
constexpr float PADDLE_EASE = 0.4f;
constexpr uint8_t BRICK_SHADE[4] = {200, 150, 110, 75}; // top row is the brightest

float clampf(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

float BreakoutPlugin::ballSpeed() const
{
  // Each cleared level makes it a little quicker.
  const float base = 0.22f * ((float)this->paramValue("ballspeed") / 100.0f);
  return base * (1.0f + 0.12f * (float)(this->level - 1));
}

void BreakoutPlugin::buildLevel(bool firstLevel)
{
  for (int i = 0; i < BRICK_ROWS * COLS; i++)
    this->bricks[i] = true;
  this->remaining = BRICK_ROWS * COLS;

  if (firstLevel)
  {
    this->level = 1;
    this->lives = 3;
    this->score = 0;
  }

  this->launchBall();
}

void BreakoutPlugin::launchBall()
{
  this->ballX = this->paddleX;
  this->ballY = (float)PADDLE_ROW - 1.0f;

  const float speed = this->ballSpeed();
  this->ballVY = -speed;
  this->ballVX = speed * (random(2) ? 0.75f : -0.75f);
}

void BreakoutPlugin::loseLife()
{
  this->holdUntil = millis() + HOLD_MS;

  if (this->lives > 0)
    this->lives--;

  if (this->lives == 0)
  {
    this->gameOver = true;
    return;
  }

  this->launchBall();
}

void BreakoutPlugin::updatePaddle(float dt)
{
  const float half = (float)this->paramValue("paddle") / 2.0f;

  if (this->isSeatHeld(0))
  {
    this->paddleTarget = this->seatPosition(0) * (float)(COLS - 1);
  }
  else
  {
    // Nobody seated: the board plays it, tracking the ball loosely.
    if (this->ballVY > 0.0f)
      this->paddleTarget = this->ballX;
    else
      this->paddleTarget += (float)this->aiDirection * 0.35f * dt;

    if (this->paddleTarget <= half || this->paddleTarget >= (float)(COLS - 1) - half)
      this->aiDirection = -this->aiDirection;
  }

  this->paddleTarget = clampf(this->paddleTarget, half - 0.5f, (float)(COLS - 1) - half + 0.5f);
  // Framerate-independent smoothing.
  const float k = 1.0f - powf(1.0f - PADDLE_EASE, dt);
  this->paddleX += (this->paddleTarget - this->paddleX) * k;
}

void BreakoutPlugin::updateBall(float dt)
{
  if (millis() < this->holdUntil)
    return;

  if (this->gameOver)
  {
    this->gameOver = false;
    this->buildLevel(true);
    return;
  }

  this->ballX += this->ballVX * dt;
  this->ballY += this->ballVY * dt;

  // Side walls and ceiling.
  if (this->ballX < 0.0f)
  {
    this->ballX = -this->ballX;
    this->ballVX = -this->ballVX;
  }
  else if (this->ballX > (float)(COLS - 1))
  {
    this->ballX = 2.0f * (float)(COLS - 1) - this->ballX;
    this->ballVX = -this->ballVX;
  }
  if (this->ballY < 0.0f)
  {
    this->ballY = -this->ballY;
    this->ballVY = -this->ballVY;
  }

  // Bricks.
  const int bx = (int)roundf(this->ballX);
  const int by = (int)roundf(this->ballY);
  if (by >= 0 && by < BRICK_ROWS && bx >= 0 && bx < COLS)
  {
    const int index = by * COLS + bx;
    if (this->bricks[index])
    {
      this->bricks[index] = false;
      this->remaining--;
      this->score++;
      this->ballVY = -this->ballVY;
      this->ballY += this->ballVY;

      if (this->remaining == 0)
      {
        this->level++;
        this->holdUntil = millis() + HOLD_MS;
        this->buildLevel(false);
        return;
      }
    }
  }

  // Paddle.
  const float half = (float)this->paramValue("paddle") / 2.0f;
  if (this->ballVY > 0.0f && this->ballY >= (float)PADDLE_ROW - 1.0f)
  {
    if (fabsf(this->ballX - this->paddleX) <= half + 0.5f)
    {
      this->ballY = (float)PADDLE_ROW - 1.0f;
      this->ballVY = -this->ballVY;
      // Where it lands on the paddle sets the outgoing angle.
      this->ballVX += (this->ballX - this->paddleX) / half * 0.14f;
      this->ballVX = clampf(this->ballVX, -0.42f, 0.42f);
    }
    else if (this->ballY > (float)PADDLE_ROW)
    {
      this->loseLife();
    }
  }
}

void BreakoutPlugin::render()
{
  Screen.clear();

  for (int y = 0; y < BRICK_ROWS; y++)
    for (int x = 0; x < COLS; x++)
      if (this->bricks[y * COLS + x])
        Screen.setPixel(x, y, 1, BRICK_SHADE[y]);

  const int size = this->paramValue("paddle");
  const uint8_t shade = this->isSeatHeld(0) ? 255 : 140;
  const float left = this->paddleX - (float)size / 2.0f + 0.5f;
  pixelfx::barH(PADDLE_ROW, left, left + (float)size, shade);

  if (millis() < this->holdUntil)
  {
    // The pause after a lost life is the moment the count matters, so the score
    // goes up top and the lives left sit under it as dots. Keeping them on
    // screen during play only put dim pixels in the ball's path.
    std::vector<int> digits;
    if (this->score >= 10)
      digits.push_back((this->score / 10) % 10);
    digits.push_back(this->score % 10);
    Screen.drawNumbers(digits.size() > 1 ? 3 : 6, 4, digits, MAX_BRIGHTNESS);

    const int shown = this->lives > 3 ? 3 : this->lives;
    const int startX = 8 - (shown * 2 - 1) / 2 - 1;
    for (int i = 0; i < shown; i++)
      Screen.setPixel(startX + i * 2, 12, 1, 160);

    return;
  }

  pixelfx::dot(this->ballX, this->ballY, MAX_BRIGHTNESS);
}

void BreakoutPlugin::setup()
{
  this->useSpeed();
  this->useSeats(1);
  this->addParam("paddle", "Paddle size", 3, 7, 5);
  this->addParam("ballspeed", "Ball speed", 40, 220, 100);

  this->paddleX = 8.0f;
  this->paddleTarget = 8.0f;
  this->gameOver = false;
  this->holdUntil = 0;
  this->lastStepAt = 0;
  this->buildLevel(true);

  Screen.clear();
}

void BreakoutPlugin::loop()
{
  if (!timer.isReady(this->scaled(STEP_MS)))
    return;

  const unsigned long now = millis();
  float dt = this->lastStepAt ? (float)(now - this->lastStepAt) / (float)STEP_MS : 1.0f;
  this->lastStepAt = now;
  if (dt > 3.0f)
    dt = 3.0f;

  this->updateSeats();
  this->updatePaddle(dt);
  this->updateBall(dt);
  this->render();
}

String BreakoutPlugin::getStatus() const
{
  String status = "Score ";
  status += this->score;
  status += "  Lv ";
  status += this->level;
  status += "  ";
  status += this->lives;
  status += this->lives == 1 ? " life" : " lives";
  return status;
}

char BreakoutPlugin::getAxis() const
{
  return 'x';
}

const char *BreakoutPlugin::getName() const
{
  return "Breakout";
}
