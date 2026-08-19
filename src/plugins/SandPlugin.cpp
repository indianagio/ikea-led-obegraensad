#include "plugins/SandPlugin.h"

namespace
{
constexpr int SPAWN_X = COLS / 2;     // the heap grows under this column
constexpr unsigned long STEP_MS = 20; // physics tick
constexpr unsigned long HOLD_FULL_MS = 3000;
constexpr float GRAVITY = 0.055f;     // cells per step, per step
constexpr float SLIDE_FRICTION = 0.45f; // a grain loses speed when it slides

// A settled grain keeps a fixed brightness derived from where it landed, so the
// heap has grain instead of reading as one flat block.
uint8_t grainShade(int x, int y)
{
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (uint8_t)(140 + (h % 116)); // 140..255
}
} // namespace

bool SandPlugin::occupied(int x, int y) const
{
  if (x < 0 || x >= COLS || y < 0 || y >= ROWS)
    return true; // walls and floor count as solid
  return grid[y * COLS + x] != 0;
}

void SandPlugin::scheduleNextGrain()
{
  // Spread the 256 grains evenly over the requested fill time. If the slot is
  // shorter than a grain's flight the pour simply runs back to back.
  const unsigned long fillMs = (unsigned long)this->paramValue("fill") * 60000ul;
  const unsigned long slot = fillMs / (unsigned long)TOTAL_PIXELS;
  this->nextSpawnAt = this->fillStartedAt + (unsigned long)this->settled * slot;
}

void SandPlugin::spawnGrain()
{
  int x = SPAWN_X + (int)random(-1, 2);
  if (x < 0)
    x = 0;
  if (x >= COLS)
    x = COLS - 1;

  if (occupied(x, 0))
  {
    // The heap has reached the spout: nowhere left to pour.
    this->full = true;
    this->grainActive = false;
    this->holdUntil = millis() + HOLD_FULL_MS;
    return;
  }

  this->grainX = x;
  this->grainY = 0;
  this->velocity = 0.0f;
  this->carry = 0.0f;
  this->grainActive = true;
}

void SandPlugin::restart()
{
  memset(this->grid, 0, sizeof(this->grid));
  this->settled = 0;
  this->full = false;
  this->grainActive = false;
  this->fillStartedAt = millis();
  this->scheduleNextGrain();
}

bool SandPlugin::stepGrain()
{
  const int x = this->grainX;
  const int y = this->grainY;

  if (!occupied(x, y + 1))
  {
    this->grainY = y + 1;
    return true;
  }

  // Slide off the heap. Requiring the side cell to be free as well stops grains
  // from squeezing diagonally through a closed gap, which is what gives the
  // pile its angle of repose instead of a spike.
  bool left = !occupied(x - 1, y + 1) && !occupied(x - 1, y);
  bool right = !occupied(x + 1, y + 1) && !occupied(x + 1, y);

  if (left && right)
  {
    if (random(2))
      left = false;
    else
      right = false;
  }

  if (left || right)
  {
    this->grainX = left ? x - 1 : x + 1;
    this->grainY = y + 1;
    this->velocity *= SLIDE_FRICTION; // scraping down the slope costs speed
    return true;
  }

  this->grid[y * COLS + x] = grainShade(x, y);
  this->settled++;
  this->grainActive = false;
  this->scheduleNextGrain();
  return false;
}

void SandPlugin::setup()
{
  this->useSpeed();
  this->addParam("fill", "Fill time (min)", 1, 120, 10);

  Screen.clear();
  this->restart();
}

void SandPlugin::loop()
{
  if (!timer.isReady(this->scaled(STEP_MS)))
    return;

  if (this->full)
  {
    if (millis() >= this->holdUntil)
      this->restart();
  }
  else if (this->grainActive)
  {
    // Ease in: the grain accelerates under gravity and covers whole cells only
    // once enough sub-cell travel has accumulated.
    this->velocity += GRAVITY;
    this->carry += this->velocity;

    while (this->carry >= 1.0f && this->grainActive)
    {
      this->carry -= 1.0f;
      this->stepGrain();
    }
  }
  else if (millis() >= this->nextSpawnAt)
  {
    this->spawnGrain();
  }

  Screen.clear();
  for (int i = 0; i < ROWS * COLS; i++)
  {
    if (this->grid[i])
      Screen.setPixelAtIndex(i, 1, this->grid[i]);
  }
  if (this->grainActive)
    Screen.setPixel(this->grainX, this->grainY, 1, MAX_BRIGHTNESS);
}

const char *SandPlugin::getName() const
{
  return "Sand";
}
