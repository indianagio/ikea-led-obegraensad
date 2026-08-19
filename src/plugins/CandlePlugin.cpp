#include "plugins/CandlePlugin.h"

#include <math.h>

namespace
{
// Geometry of the flame, in pixel coordinates (y = 0 is the top row).
constexpr float BASE_Y = 14.0f;   // where the flame sits
constexpr float CENTER_X = 8.0f;  // between columns 7 and 8
constexpr float BASE_WIDTH = 3.0f; // half-width of the flame at its widest
} // namespace

float CandlePlugin::hash(uint32_t a, uint32_t b)
{
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (float)(h & 0xffff) / 65535.0f;
}

// Value noise: smooth interpolation between two hashed lattice points, so the
// flame drifts instead of jittering from frame to frame.
float CandlePlugin::noise(int row, float t) const
{
  uint32_t i = (uint32_t)t;
  float f = t - (float)i;
  f = f * f * (3.0f - 2.0f * f);

  float a = hash((uint32_t)row, i);
  float b = hash((uint32_t)row, i + 1);
  return a + (b - a) * f;
}

void CandlePlugin::updateMotion()
{
  const float tall = (float)this->heightPct / 100.0f;
  const float lean = (float)this->swayPct / 100.0f;
  const float flick = (float)this->flickerPct / 100.0f;

  this->phase += 0.45f;

  // The flame keeps picking new resting points, and often enough that it never
  // settles into a drift.
  if (random(100) < (int)(30.0f * flick))
  {
    this->swayTarget = (float)random(-100, 101) / 100.0f * 1.8f * lean;
    this->heightTarget = (8.5f + (float)random(0, 100) / 100.0f * 4.5f) * tall;
    // Brightness swing is measured from full, so flicker=0 means rock steady.
    float swing = (0.74f + (float)random(0, 100) / 100.0f * 0.34f) - 1.0f;
    this->gainTarget = 1.0f + swing * flick;
  }

  // Snap: a real flame flicks. Skip the easing entirely and jump.
  if (random(100) < (int)(14.0f * flick))
  {
    this->sway = this->swayTarget;
    this->gain = this->gainTarget;
  }

  // Draft: short, sharp duck-and-lean.
  if (this->gustTicks == 0 && random(1000) < (int)(40.0f * flick))
  {
    this->gustTicks = random(3, 9);
    this->swayTarget =
        (random(2) ? 1.0f : -1.0f) * (2.1f + (float)random(0, 90) / 100.0f) * lean;
    this->heightTarget = (5.5f + (float)random(0, 100) / 100.0f * 2.5f) * tall;
    this->gainTarget = 1.0f + 0.10f * flick;
    this->sway = this->swayTarget * 0.6f; // the lean starts immediately
  }
  else if (this->gustTicks > 0)
  {
    this->gustTicks--;
  }

  // Tight tracking: the flame reaches its target in a few frames, not a second.
  this->sway += (this->swayTarget - this->sway) * 0.45f;
  this->height += (this->heightTarget - this->height) * 0.38f;
  this->gain += (this->gainTarget - this->gain) * 0.55f;
}

void CandlePlugin::drawFlame()
{
  for (int y = 0; y < ROWS; y++)
  {
    float u = (BASE_Y - (float)y) / this->height; // 0 at the base, 1 at the tip
    if (u < 0.0f || u > 1.0f)
      continue;

    // Two octaves: a slow waver plus a quicker one that only bites near the tip.
    float wobble = (this->noise(y, this->phase * 0.70f) - 0.5f) * 1.6f * u +
                   (this->noise(y + 97, this->phase * 2.20f) - 0.5f) * 0.8f * u * u;

    float axis = CENTER_X + this->sway * u * u + wobble;

    // Teardrop: rounded at the base, tapering to a point.
    float w = BASE_WIDTH * ((float)this->widthPct / 100.0f) * powf(1.0f - u, 0.6f);
    if (u < 0.20f)
      w *= 0.55f + 0.45f * (u / 0.20f);
    if (w < 0.35f)
      w = 0.35f;

    float core = 1.0f - 0.45f * u;

    for (int x = 0; x < COLS; x++)
    {
      float d = fabsf((float)x + 0.5f - axis);
      float r = d / w;
      if (r >= 1.0f)
        continue;

      float intensity = core * (1.0f - r * r) * this->gain;

      // Turbulence, strongest towards the tip where the flame is thinnest.
      intensity *= 1.0f - 0.30f * (1.0f - this->noise(y * 31 + x, this->phase * 1.8f)) *
                              (0.35f + 0.65f * u);

      int b = (int)(intensity * 255.0f);
      if (b > 255)
        b = 255;
      if (b < 8) // below this the pixel only buzzes
        continue;

      Screen.setPixel(x, y, 1, (uint8_t)b);
    }
  }
}

void CandlePlugin::setup()
{
  this->useSpeed();
  this->addParam("height", "Height", 40, 160, 100);
  this->addParam("width", "Width", 50, 150, 100);
  this->addParam("sway", "Sway", 0, 200, 100);
  this->addParam("flicker", "Flicker", 0, 200, 100);
  this->syncParams();
  this->phase = 0.0f;
  this->sway = 0.0f;
  this->swayTarget = 0.0f;
  this->height = 11.0f * ((float)this->heightPct / 100.0f);
  this->heightTarget = this->height;
  this->gain = 1.0f;
  this->gainTarget = 1.0f;
  this->gustTicks = 0;

  Screen.clear();
}

void CandlePlugin::loop()
{
  if (!timer.isReady(this->scaled(33)))
    return;

  this->updateMotion();

  Screen.clear();
  this->drawFlame();
}

const char *CandlePlugin::getName() const
{
  return "Candle";
}

void CandlePlugin::syncParams()
{
  this->heightPct = (uint8_t)this->paramValue("height");
  this->widthPct = (uint8_t)this->paramValue("width");
  this->swayPct = (uint8_t)this->paramValue("sway");
  this->flickerPct = (uint8_t)this->paramValue("flicker");
}

void CandlePlugin::onParamChanged(const char *key, int value)
{
  this->syncParams();

  // Apply straight away: the random re-pick that would otherwise carry the new
  // value is itself gated by flicker, so at flicker=0 it would never run.
  if (!strcmp(key, "height"))
    this->heightTarget = 11.0f * ((float)this->heightPct / 100.0f);
  else if (!strcmp(key, "sway") && this->swayPct == 0)
    this->swayTarget = 0.0f;
  else if (!strcmp(key, "flicker") && this->flickerPct == 0)
    this->gainTarget = 1.0f; // steady flame instead of frozen at a random dim
}
