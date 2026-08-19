# What this fork adds

This is a fork of [ph1p/ikea-led-obegraensad](https://github.com/ph1p/ikea-led-obegraensad).
The upstream README still applies for wiring, flashing, OTA and the HTTP API;
this file only covers what is different here.

Three things grew out of the original: a way for plugins to expose settings
without touching the web UI, a way for phones to act as game controllers, and
six new plugins that use both. There is also some housekeeping that was needed
along the way and turned out to matter more than expected.

## Plugin parameters

A plugin registers its knobs once and the web UI builds a slider for each:

```cpp
void MyPlugin::setup()
{
  useSpeed();                                   // the standard Speed knob
  addParam("gap", "Gap size", 3, 8, 5);         // key, label, min, max, default
}

void MyPlugin::loop()
{
  if (!timer.isReady(scaled(33)))               // scaled() honours Speed
    return;

  const int gap = paramValue("gap");
}
```

`addParam` is idempotent: calling it again from `setup()` keeps whatever value
the user picked, so a plugin can be re-activated without losing its settings.
Override `onParamChanged(key, value)` if a change needs to be applied
immediately rather than at the next natural opportunity.

The parameters of the active plugin travel in the `info` websocket message and
are set with `{"event":"param","key":"gap","value":6}`. Nothing in the frontend
knows about any specific plugin, so a new knob costs no frontend work.

The animation plugins that had a single frame timer now route it through
`scaled()`, which gives each of them a Speed slider. Plugins with no
frame timer (the clocks, Weather, Draw, ArtNet, DDP) expose nothing and the
settings section disappears on its own.

The older ArtNet and Game of Life sidebar sections are still hardcoded and were
left alone. The ArtNet one is worth knowing about: it targets plugin id 17,
which ArtNet has not had for a long time, so it never appears.

## Playing on phones

Games declare how many players they take and how they want to be steered. The
base `Plugin` class owns the rest: claiming a seat, proving ownership, releasing
a seat that has gone quiet, and telling everyone what changed.

```cpp
void MyGame::setup()
{
  useSeats(1);            // or 2
}

char MyGame::getAxis() const { return 'x'; }   // 'x' 'y' slide, 'd' d-pad, 'b' button only
bool MyGame::hasButton() const { return true; }

void MyGame::loop()
{
  updateSeats();                        // releases silent seats

  if (isSeatHeld(0))
  {
    shipX = seatPosition(0) * (COLS - 1);   // 0..1 along the control axis
    if (takePress(0))                       // one queued button press
      fire();
  }
  else
  {
    playItYourself();                   // an empty seat is played by the board
  }
}
```

`getStatus()` returns the line shown at the top of the controller, so each game
decides what is worth reading: `"3 : 1"` for Pong, `"Score 12 Wave 2 3 lives"`
for Invaders.

### The controller page

Open `http://<board>/#/play` on a phone, pick a side, and you get the control
the board asked for: a vertical or horizontal drag area, a d-pad that also
accepts swipes, or a tap-anywhere button. Games that both steer and shoot get a
separate Fire button. Two phones on the same network can each take a seat in
Pong; whichever seat nobody claims is played by the board, and its paddle is
drawn dimmer so you can tell at a glance.

A device keeps a random token in `localStorage`. Seats belong to that token
rather than to a websocket connection, which matters because a phone drops and
reopens its connection every time the screen sleeps. A seat with no input for
nine seconds goes back to the board; the page sends a keepalive every 2.5s.

Pointer input is coalesced onto a 30ms cadence. A phone fires `pointermove` far
faster than the game steps, and one message per event only gives the board more
JSON to parse for the same result.

### Protocol

Everything goes through one websocket event:

```json
{"event":"gamepad","action":"join","seat":1,"token":"a1b2c3d4"}
{"event":"gamepad","action":"move","seat":1,"token":"a1b2c3d4","pos":42}
{"event":"gamepad","action":"dir","seat":1,"token":"a1b2c3d4","dir":"up"}
{"event":"gamepad","action":"btn","seat":1,"token":"a1b2c3d4"}
{"event":"gamepad","action":"ping","seat":1,"token":"a1b2c3d4"}
{"event":"gamepad","action":"leave","seat":1,"token":"a1b2c3d4"}
{"event":"gamepad","action":"state"}
```

`pos` is 0-100 along the control axis. Button presses are queued as edges (up to
three) and consumed one at a time, so holding the button cannot auto-repeat.

The board answers with:

```json
{"event":"game","taken":[true,false],"axis":"x","button":true,"status":"Score 0"}
```

The same fields also ride along in every `info` message. That is deliberate: a
controller already open on a phone has no other way to learn that the board
switched to a game steered differently.

## Live preview

The board can stream its framebuffer so the web UI shows what the panel is
doing for every mode, not just for Draw.

```json
{"event":"preview","enabled":1}
```

Frames then arrive as raw binary, 256 bytes, one per pixel, at 20fps. This
started out as JSON and was about a kilobyte per frame, built with String
concatenation inside the task that also runs the game loop; the periodic hitch
that caused was visible in gameplay.

Subscriptions are tracked per client id rather than as one global flag. With a
single flag, a browser going back to Draw would cut the stream to the phones in
the middle of a game.

## Sub-pixel rendering

`include/pixelfx.h` draws bars and dots with fractional coverage:

```cpp
pixelfx::barV(x, top, bottom, 255);   // vertical bar, fractional rows
pixelfx::barH(y, left, right, 255);   // horizontal bar, fractional columns
pixelfx::dot(x, y, 255);              // bilinear splat over four pixels
```

The panel is 16x16 but has 64 grey levels, and spending brightness on partial
coverage buys a surprising amount of apparent resolution. Anything that moves
slower than one pixel per frame stutters without it; a paddle at row 7.4 lights
row 7 at 60 percent and row 8 at 40 percent and reads as genuinely being
between the two. The blend is a maximum, never subtractive, so a ball crossing
a brick does not dim it.

The games also integrate with delta time rather than assuming a fixed step, so
a frame that arrives late does not become a visible jump.

## New plugins

Registered after the existing ones, so no existing plugin id moves.

**Candle** is a procedural flame rather than a canned animation: a teardrop
silhouette whose axis, height and brightness follow smoothed random targets,
with occasional snaps that bypass the easing. Real flames flick rather than
drift, and easing alone always looks like drifting. Height, width, sway and
flicker are adjustable.

**Sand** pours grains from the top and lets them pile up. Grains accelerate
under gravity and lose speed sliding down the slope. The angle of repose comes
from refusing to let a grain squeeze diagonally through a closed gap. A fill
time parameter spreads the 256 grains over anything from one minute to two
hours, so it can be used as a very slow clock; below about a minute and a half
the physics becomes the limit and grains simply pour back to back.

**Drop** is a pool of water with drops falling into it, modelled as a 1D wave
equation with reflecting walls. The impact impulse is mean-zero and the surface
is re-centred every step; without both, each drop permanently lowered the water
level and the pool visibly drained. The surface is a soft sheen over a body that
fades with depth, with extra highlight on moving crests, rather than a hard
bright line.

**Pong** takes two players. The outgoing angle depends on where the ball meets
the paddle, and any empty side is played by the board.

**Flappy** is one button. Pipes scroll with sub-pixel columns, and when a pipe
leaves the screen it is placed behind the furthest pipe rather than at a fixed
offset, so spacing stays even however long the game runs.

**Invaders** is a slide plus a fire button. One shot in flight at a time, as in
the original. Only invaders with a clear column below them drop bombs, and the
fleet speeds up as you thin it out and again on each wave.

## Two games that were already there

**Breakout** used to be a demo: the paddle swept back and forth on its own, and
`loop()` blocked with `vTaskDelay(random(100, 200))` inside the drawing task.
It is now a real game with bricks, lives and levels, steered by a seated player,
and it no longer blocks.

**Snake** keeps its original pathfinding AI for when nobody is playing. Under
player control it holds its heading, refuses a 180 degree reversal, and is
allowed to enter the cell its tail is vacating unless that same step eats and
the body grows. Without that last exception, tight turns killed you unfairly.

## Panel power and the side button

The original lamp button gets three gestures:

| gesture | action |
| --- | --- |
| single press | next plugin |
| double press | the plugin marked as default |
| hold 1-3s, then release | panel on/off |
| hold past 3s | a countdown appears on the panel |
| hold to 10s | clears WiFi credentials and restarts |

Power is not brightness set to zero. `PIN_ENABLE` is the shift registers'
active-low output enable, so driving it high blanks the panel in hardware while
WiFi, OTA and the API stay up, and the active plugin idles instead of animating
into the dark. The state persists across a power cycle, like a lamp. There is
also an on/off button in the web UI and a `{"event":"power","on":0}` websocket
event; all three stay in sync.

The power toggle fires on release rather than at one second, so the panel stays
lit long enough to show the WiFi countdown. Releasing during the countdown
aborts cleanly and does not toggle power.

After a WiFi reset the board restarts, finds no credentials, and opens the
setup hotspot as it already did on a fresh install. Note that you rarely need
this: WiFiManager already opens the hotspot by itself whenever it cannot
connect at boot, so moving the lamp to another network needs no button at all.
The hold is for the case where the old network is still reachable and you want
to change it anyway. It is deliberately not exposed over HTTP, since anyone on
the network could then take the lamp offline.

ButtonFever accepts only one press-for threshold and it is spent on the one
second hold, so the longer holds are handled by reading the pin directly. That
threshold has to stay registered even so: its presence is what stops a long
press from also being reported as a single press.

## Things worth knowing before you build

The web UI is not served from files. `pnpm build` in `frontend/` compresses the
whole page into `src/webgui.cpp` as a PROGMEM array, so any UI change needs the
frontend rebuilt before the firmware, or the board keeps serving the old page.

`platformio.ini` sets `upload_speed = 115200` for `esp32dev`. The board
definition defaults to 460800, which a CH340 adapter cannot sustain: esptool
loads the stub, raises the baud rate and then loses the chip with "The chip
stopped responding". If your adapter is better than mine, raising it back will
flash about four times faster.

`getLocalTime()` is called with a 5ms timeout instead of the 5000ms default.
While the clock is unsynced that call stalls for a full five seconds, and it
runs from `loop()` once every four iterations, which pinned the main loop at
roughly 0.8Hz. Everything living in `loop()` suffered from that: the button
poll, `ElegantOTA.loop()`, the scheduler and WiFi reconnection. It is the kind
of bug that only shows up as "the button feels broken sometimes".

Flash usage sits at about 76 percent of the 1.9MB application partition, so
there is room for a good deal more.
