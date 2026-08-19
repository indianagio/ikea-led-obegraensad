#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include <vector>

#include "screen.h"
#include "signs.h"
#include "websocket.h"

// One tunable exposed to the web UI. Plugins register these once and the UI
// renders a slider per entry, so a new knob needs no frontend change.
struct PluginParam
{
  const char *key;
  const char *label;
  int16_t min;
  int16_t max;
  int16_t value;
};

class Plugin
{
private:
  int id;

protected:
  std::vector<PluginParam> params;

  // Register a knob. Idempotent: re-registering keeps the current value, so a
  // plugin can call this from setup() without losing what the user picked.
  void addParam(const char *key, const char *label, int16_t min, int16_t max, int16_t value);
  int16_t paramValue(const char *key) const;

  // Registers the standard "Speed" knob and is the hook the animation plugins
  // run their frame interval through.
  void useSpeed(int16_t value = 100);
  uint16_t scaled(uint16_t intervalMs) const;

  // --- Player seats -------------------------------------------------------
  // A controllable plugin declares how many players it takes; the base class
  // owns claiming, ownership and release so every game behaves the same.
  uint8_t seatCount = 0;
  bool seatHeld[2] = {false, false};
  String seatTok[2] = {"", ""};
  unsigned long seatAt[2] = {0, 0};
  float seatY[2] = {0.5f, 0.5f}; // 0..1 along the control axis
  uint8_t seatDir[2] = {0, 0};   // 0 none, 1 up, 2 right, 3 down, 4 left
  uint8_t seatPress[2] = {0, 0}; // action-button presses waiting to be consumed

  void useSeats(uint8_t count);
  float seatPosition(uint8_t index) const;
  uint8_t seatDirection(uint8_t index) const;
  // Consumes one queued press. Buttons are edges, not states: a game must
  // take each press exactly once or holding the button would repeat forever.
  bool takePress(uint8_t index);
  void updateSeats(); // drops silent seats and announces the change

public:
  Plugin();

  virtual ~Plugin()
  {
  }

  virtual void teardown();
  virtual void websocketHook(JsonDocument &request);
  virtual void setup() = 0;
  virtual void loop();
  virtual const char *getName() const = 0;

  void setId(int id);
  int getId() const;

  uint8_t getSeatCount() const;
  bool isSeatHeld(uint8_t index) const;
  bool handleGamepad(JsonDocument &request);
  void broadcastGame();
  virtual String getStatus() const;
  virtual char getAxis() const; // 'y' vertical pad, 'x' horizontal, 'd' d-pad, 'b' none
  virtual bool hasButton() const; // game wants an action button too

  const std::vector<PluginParam> &getParams() const;
  bool setParam(const char *key, int value);
  virtual void onParamChanged(const char *key, int value);
};

class PluginManager
{
private:
  std::vector<Plugin *> plugins;
  Plugin *activePlugin = nullptr;
  int nextPluginId;
  int persistedPluginId = 1;

  void renderPluginId(int pluginId);

public:
  PluginManager();

  int addPlugin(Plugin *plugin);
  void setActivePlugin(const char *pluginName);
  void setActivePluginById(int pluginId);
  void runActivePlugin();
  void setupActivePlugin();
  void activateNextPlugin();
  void persistActivePlugin();
  void init();
  void activatePersistedPlugin();
  int getPersistedPluginId();
  Plugin *getActivePlugin() const;
  std::vector<Plugin *> &getAllPlugins();
  size_t getNumPlugins();
};

extern PluginManager pluginManager;
