#include "plugins/ClockXLPlugin.h"

#include "pixelfx.h"

#include <sys/time.h>
#include <time.h>

namespace
{
constexpr unsigned long STEP_MS = 100;

// 7x7 digits with two-pixel stems. Drawn for a panel read from across a room,
// so the shapes are chosen to stay apart from each other rather than to look
// like a typeface: 1 keeps a foot and a flag so it is not a bare stroke, 6 and
// 9 break their bowls on opposite sides, 0 stays open in the middle where 8 is
// closed, and 2 leans on a full base while 3 keeps both bowls on the right.
constexpr uint8_t DIGITS[10][7] = {
    {0b0111110, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b0111110}, // 0
    {0b0011000, 0b0111000, 0b0011000, 0b0011000, 0b0011000, 0b0011000, 0b0111110}, // 1
    {0b0111110, 0b1100011, 0b0000011, 0b0001110, 0b0111000, 0b1100000, 0b1111111}, // 2
    {0b0111110, 0b1100011, 0b0000011, 0b0011110, 0b0000011, 0b1100011, 0b0111110}, // 3
    {0b0001110, 0b0011110, 0b0110110, 0b1100110, 0b1111111, 0b0000110, 0b0000110}, // 4
    {0b1111111, 0b1100000, 0b1111110, 0b0000011, 0b0000011, 0b1100011, 0b0111110}, // 5
    {0b0011110, 0b0110000, 0b1100000, 0b1111110, 0b1100011, 0b1100011, 0b0111110}, // 6
    {0b1111111, 0b0000110, 0b0001100, 0b0011000, 0b0011000, 0b0011000, 0b0011000}, // 7
    {0b0111110, 0b1100011, 0b1100011, 0b0111110, 0b1100011, 0b1100011, 0b0111110}, // 8
    {0b0111110, 0b1100011, 0b1100011, 0b0111111, 0b0000011, 0b0000011, 0b0111110}, // 9
};

// 7 + 2 + 7 fills the panel exactly, in both directions.
constexpr int CELL = 7;
constexpr int COL_LEFT = 0;
constexpr int COL_RIGHT = 9;
constexpr int ROW_TOP = 0;
constexpr int ROW_BOTTOM = 9;
constexpr int SECONDS_ROW = 7; // two rows tall, filling the gap
} // namespace

void ClockXLPlugin::drawDigit(int x, int y, uint8_t digit, uint8_t shade)
{
  if (digit > 9)
    return;

  for (int row = 0; row < CELL; row++)
  {
    const uint8_t bits = DIGITS[digit][row];
    for (int col = 0; col < CELL; col++)
    {
      if (bits & (1 << (CELL - 1 - col)))
        Screen.setPixel(x + col, y + row, 1, shade);
    }
  }
}

void ClockXLPlugin::drawWaiting()
{
  // A dash where each digit will be, so an unsynced clock looks like it is
  // waiting rather than like a dead panel. The other clock plugins draw
  // nothing at all in this state, which is what makes them look broken.
  const int xs[2] = {COL_LEFT, COL_RIGHT};
  const int ys[2] = {ROW_TOP, ROW_BOTTOM};

  for (int yi = 0; yi < 2; yi++)
    for (int xi = 0; xi < 2; xi++)
      for (int col = 1; col < CELL - 1; col++)
        Screen.setPixel(xs[xi] + col, ys[yi] + CELL / 2, 1, 35);
}

void ClockXLPlugin::setup()
{
  this->addParam("minutes", "Minutes level", 20, 100, 70);
  this->addParam("seconds", "Seconds bar", 0, 100, 45);
  this->addParam("hour12", "12-hour clock", 0, 1, 0);

  this->hasTime = false;
  Screen.clear();
}

void ClockXLPlugin::loop()
{
  if (!timer.isReady(STEP_MS))
    return;

  struct tm timeinfo;
  // Short timeout: this runs in the drawing task, and the 5000ms default would
  // stall everything else for five seconds per frame until the clock syncs.
  this->hasTime = getLocalTime(&timeinfo, 5);

  Screen.clear();

  if (!this->hasTime)
  {
    this->drawWaiting();
    return;
  }

  int hour = timeinfo.tm_hour;
  if (this->paramValue("hour12"))
  {
    hour %= 12;
    if (hour == 0)
      hour = 12;
  }

  const uint8_t hourShade = MAX_BRIGHTNESS;
  const uint8_t minuteShade = (uint8_t)(MAX_BRIGHTNESS * this->paramValue("minutes") / 100);

  this->drawDigit(COL_LEFT, ROW_TOP, hour / 10, hourShade);
  this->drawDigit(COL_RIGHT, ROW_TOP, hour % 10, hourShade);
  this->drawDigit(COL_LEFT, ROW_BOTTOM, timeinfo.tm_min / 10, minuteShade);
  this->drawDigit(COL_RIGHT, ROW_BOTTOM, timeinfo.tm_min % 10, minuteShade);

  const int secondsShade = this->paramValue("seconds");
  if (secondsShade > 0)
  {
    // The two free rows between the digits are given over to the minute, which
    // runs across them. Sub-second precision plus sub-pixel drawing turns what
    // would be a pixel jumping every four seconds into a steady creep.
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    const float fraction =
        ((float)timeinfo.tm_sec + (float)tv.tv_usec / 1000000.0f) / 60.0f;

    const uint8_t shade = (uint8_t)(MAX_BRIGHTNESS * secondsShade / 100);
    pixelfx::barH(SECONDS_ROW, 0.0f, fraction * (float)COLS, shade);
    pixelfx::barH(SECONDS_ROW + 1, 0.0f, fraction * (float)COLS, shade);
  }
}

const char *ClockXLPlugin::getName() const
{
  return "Clock XL";
}
