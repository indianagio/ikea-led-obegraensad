#include "plugins/DropPlugin.h"

#include <math.h>

namespace
{
constexpr float SPREAD = 0.22f;   // how fast a disturbance travels sideways
constexpr float GRAVITY = 0.055f; // for the droplet and the splash
constexpr float MAX_DISPLACEMENT = 3.5f;
constexpr unsigned long STEP_MS = 33;

// Water reads as depth, not as an outline: a soft sheen on the surface over a
// body that fades as it gets deeper. A hard white top line looks like a wall.
constexpr uint8_t SURFACE_BRIGHT = 150;
constexpr uint8_t BODY_NEAR = 70; // just under the surface
constexpr uint8_t BODY_FAR = 22;  // at the bottom of the pool
constexpr uint8_t CREST_BRIGHT = 90; // extra sheen on a steep moving crest

int clampCol(int x)
{
  if (x < 0)
    return 0;
  if (x >= COLS)
    return COLS - 1;
  return x;
}
} // namespace

float DropPlugin::baseline() const
{
  // "level" is how many rows of water are at rest, counted up from the floor.
  return (float)ROWS - (float)this->paramValue("level");
}

float DropPlugin::surfaceAt(int x) const
{
  return this->baseline() - this->h[clampCol(x)];
}

void DropPlugin::disturb(int col, float strength)
{
  col = clampCol(col);
  this->v[col] -= strength;
  if (col > 0)
    this->v[col - 1] -= strength * 0.45f;
  if (col < COLS - 1)
    this->v[col + 1] -= strength * 0.45f;

  // The impulse above only pushes down, which would drain the pool a little
  // with every drop. Water is incompressible: give back what we took by
  // raising the rest of the surface.
  float total = strength * (1.0f + (col > 0 ? 0.45f : 0.0f) + (col < COLS - 1 ? 0.45f : 0.0f));
  float perColumn = total / (float)COLS;
  for (int x = 0; x < COLS; x++)
    this->v[x] += perColumn;
}

void DropPlugin::releaseDrop()
{
  this->dropX = (float)random(2, COLS - 2);
  this->dropY = -1.0f;
  this->dropVel = 0.12f;
  this->dropActive = true;
}

void DropPlugin::updateWaves()
{
  // Discrete wave equation: each column is pulled towards the average of its
  // neighbours. Clamping the index at the edges reflects the wave off the wall.
  for (int x = 0; x < COLS; x++)
  {
    float left = this->h[clampCol(x - 1)];
    float right = this->h[clampCol(x + 1)];
    this->v[x] += (left + right - 2.0f * this->h[x]) * SPREAD;
  }

  // Damping is a knob: "calm" 0 keeps the ripples alive, 100 kills them fast.
  const float damping = 1.0f - (float)this->paramValue("calm") / 100.0f * 0.06f;
  for (int x = 0; x < COLS; x++)
    this->v[x] *= damping;

  for (int x = 0; x < COLS; x++)
  {
    this->h[x] += this->v[x];

    if (this->h[x] > MAX_DISPLACEMENT)
    {
      this->h[x] = MAX_DISPLACEMENT;
      this->v[x] = 0.0f;
    }
    else if (this->h[x] < -MAX_DISPLACEMENT)
    {
      this->h[x] = -MAX_DISPLACEMENT;
      this->v[x] = 0.0f;
    }
  }

  // Clamping and rounding leak volume over time, so re-centre the surface on
  // its resting level. Without this the pool visibly drains after a few drops.
  float mean = 0.0f;
  for (int x = 0; x < COLS; x++)
    mean += this->h[x];
  mean /= (float)COLS;

  for (int x = 0; x < COLS; x++)
    this->h[x] -= mean;
}

void DropPlugin::updateDrop()
{
  if (!this->dropActive)
  {
    if (millis() >= this->nextDropAt)
      this->releaseDrop();
    return;
  }

  this->dropVel += GRAVITY;
  this->dropY += this->dropVel;

  const int col = clampCol((int)(this->dropX + 0.5f));
  if (this->dropY < this->surfaceAt(col))
    return;

  // Impact: push the surface down and throw a few pixels up.
  this->disturb(col, 1.35f);
  this->dropActive = false;
  const unsigned long rate = (unsigned long)this->paramValue("rate") * 1000ul;
  this->nextDropAt = millis() + rate / 2 + (unsigned long)random(0, (long)rate + 1);

  const int count = (int)random(3, 5);
  int spawned = 0;
  for (int i = 0; i < (int)(sizeof(this->particles) / sizeof(this->particles[0])); i++)
  {
    if (spawned >= count)
      break;
    if (this->particles[i].alive)
      continue;

    this->particles[i].x = (float)col + 0.5f;
    this->particles[i].y = this->surfaceAt(col) - 0.4f;
    this->particles[i].vx = (float)random(-45, 46) / 100.0f;
    this->particles[i].vy = -(0.30f + (float)random(0, 45) / 100.0f);
    this->particles[i].alive = true;
    spawned++;
  }
}

void DropPlugin::updateParticles()
{
  for (auto &p : this->particles)
  {
    if (!p.alive)
      continue;

    p.vy += GRAVITY;
    p.x += p.vx;
    p.y += p.vy;

    if (p.x < 0.0f || p.x >= (float)COLS)
    {
      p.alive = false;
      continue;
    }

    // Only a falling particle can land, otherwise it dies on the way up.
    const int col = clampCol((int)p.x);
    if (p.vy > 0.0f && p.y >= this->surfaceAt(col))
    {
      this->disturb(col, 0.22f);
      p.alive = false;
    }
  }
}

void DropPlugin::render()
{
  Screen.clear();

  const float floorY = (float)ROWS;

  for (int x = 0; x < COLS; x++)
  {
    const float sy = this->surfaceAt(x);
    const int iy = (int)floorf(sy);
    const float frac = sy - (float)iy;

    // A moving crest catches more light than flat water.
    float slope = fabsf(this->h[clampCol(x + 1)] - this->h[clampCol(x - 1)]) * 0.5f;
    float sheen = slope + fabsf(this->v[x]) * 2.0f;
    if (sheen > 1.0f)
      sheen = 1.0f;
    const int surface = SURFACE_BRIGHT + (int)(CREST_BRIGHT * sheen);

    // The surface sits between two pixels, so split it across both. That is
    // what makes a 16px-tall pool read as a smooth moving line rather than a
    // staircase.
    if (iy >= 0 && iy < ROWS)
    {
      int top = (int)(surface * (1.0f - frac));
      Screen.setPixel(x, iy, 1, (uint8_t)(top > 255 ? 255 : top));
    }

    for (int y = (iy + 1 > 0 ? iy + 1 : 0); y < ROWS; y++)
    {
      // Depth fade: bright just under the surface, dark at the bottom.
      float depth = ((float)y - sy) / (floorY - sy > 1.0f ? floorY - sy : 1.0f);
      if (depth < 0.0f)
        depth = 0.0f;
      if (depth > 1.0f)
        depth = 1.0f;

      int body = (int)(BODY_NEAR + (BODY_FAR - BODY_NEAR) * depth);

      // The pixel the surface bleeds into keeps part of the highlight.
      if (y == iy + 1)
      {
        int blended = (int)(surface * frac);
        if (blended > body)
          body = blended;
      }

      if (body > 255)
        body = 255;
      Screen.setPixel(x, y, 1, (uint8_t)body);
    }
  }

  if (this->dropActive && this->dropY >= 0.0f)
  {
    const int x = clampCol((int)(this->dropX + 0.5f));
    const int y = (int)this->dropY;

    if (y - 1 >= 0 && y - 1 < ROWS)
      Screen.setPixel(x, y - 1, 1, 70); // short trail
    if (y >= 0 && y < ROWS)
      Screen.setPixel(x, y, 1, MAX_BRIGHTNESS);
  }

  for (const auto &p : this->particles)
  {
    if (!p.alive)
      continue;
    const int x = clampCol((int)p.x);
    const int y = (int)p.y;
    if (y >= 0 && y < ROWS)
      Screen.setPixel(x, y, 1, 210);
  }
}

void DropPlugin::setup()
{
  this->useSpeed();
  this->addParam("level", "Water level", 2, 10, 5);
  this->addParam("rate", "Drop every (s)", 1, 30, 4);
  this->addParam("calm", "Calm", 0, 100, 30);
  for (int x = 0; x < COLS; x++)
  {
    this->h[x] = 0.0f;
    this->v[x] = 0.0f;
  }
  for (auto &p : this->particles)
    p.alive = false;

  this->dropActive = false;
  this->nextDropAt = millis() + 600;

  Screen.clear();
}

void DropPlugin::loop()
{
  if (!timer.isReady(this->scaled(STEP_MS)))
    return;

  this->updateWaves();
  this->updateDrop();
  this->updateParticles();
  this->render();
}

const char *DropPlugin::getName() const
{
  return "Drop";
}
