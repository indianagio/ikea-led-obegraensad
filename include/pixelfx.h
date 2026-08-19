#pragma once

#include "screen.h"

// Sub-pixel drawing helpers.
//
// The panel is only 16x16 but has 64 grey levels, so spending brightness on
// partial coverage buys a lot of apparent resolution: a paddle at row 7.4 looks
// like it is genuinely between two pixels instead of snapping to one. Without
// this, anything that moves slower than one pixel per frame stutters.
namespace pixelfx
{

// Never darken what is already there: bricks and paddles must survive the ball
// passing over them.
inline void blend(int x, int y, float coverage, uint8_t shade)
{
  if (x < 0 || x >= COLS || y < 0 || y >= ROWS || coverage <= 0.0f)
    return;

  if (coverage > 1.0f)
    coverage = 1.0f;

  const int value = (int)((float)shade * coverage);
  if (value <= 0)
    return;

  const uint8_t current = Screen.getRenderBuffer()[y * COLS + x];
  if ((uint8_t)value > current)
    Screen.setPixel(x, y, 1, (uint8_t)value);
}

// Vertical bar spanning rows [top, bottom) in fractional rows.
inline void barV(int x, float top, float bottom, uint8_t shade)
{
  for (int y = (int)floorf(top); y <= (int)floorf(bottom); y++)
  {
    const float lo = top > (float)y ? top : (float)y;
    const float hi = bottom < (float)(y + 1) ? bottom : (float)(y + 1);
    blend(x, y, hi - lo, shade);
  }
}

// Horizontal bar spanning columns [left, right) in fractional columns.
inline void barH(int y, float left, float right, uint8_t shade)
{
  for (int x = (int)floorf(left); x <= (int)floorf(right); x++)
  {
    const float lo = left > (float)x ? left : (float)x;
    const float hi = right < (float)(x + 1) ? right : (float)(x + 1);
    blend(x, y, hi - lo, shade);
  }
}

// A dot splattered over the four pixels it overlaps.
inline void dot(float x, float y, uint8_t shade)
{
  const int ix = (int)floorf(x);
  const int iy = (int)floorf(y);
  const float fx = x - (float)ix;
  const float fy = y - (float)iy;

  blend(ix, iy, (1.0f - fx) * (1.0f - fy), shade);
  blend(ix + 1, iy, fx * (1.0f - fy), shade);
  blend(ix, iy + 1, (1.0f - fx) * fy, shade);
  blend(ix + 1, iy + 1, fx * fy, shade);
}

} // namespace pixelfx
