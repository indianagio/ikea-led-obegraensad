#include "PluginManager.h"
#include "scheduler.h"

Plugin::Plugin() : id(-1)
{
}

void Plugin::setId(int id)
{
  this->id = id;
}

int Plugin::getId() const
{
  return id;
}

void Plugin::teardown()
{
}
void Plugin::loop()
{
}
void Plugin::websocketHook(JsonDocument &request)
{
}

void Plugin::onParamChanged(const char *key, int value)
{
}

void Plugin::addParam(const char *key, const char *label, int16_t min, int16_t max, int16_t value)
{
  for (const auto &param : params)
  {
    if (!strcmp(param.key, key))
      return; // already registered, keep whatever the user set
  }
  params.push_back({key, label, min, max, value});
}

int16_t Plugin::paramValue(const char *key) const
{
  for (const auto &param : params)
  {
    if (!strcmp(param.key, key))
      return param.value;
  }
  return 0;
}

void Plugin::useSpeed(int16_t value)
{
  addParam("speed", "Speed", 10, 400, value);
}

uint16_t Plugin::scaled(uint16_t intervalMs) const
{
  int16_t speed = paramValue("speed");
  if (speed <= 0)
    return intervalMs;

  uint32_t result = (uint32_t)intervalMs * 100u / (uint32_t)speed;
  if (result < 1)
    result = 1;
  if (result > 60000)
    result = 60000;
  return (uint16_t)result;
}

// --- Player seats ---------------------------------------------------------

namespace
{
constexpr unsigned long SEAT_TIMEOUT_MS = 9000; // a silent seat goes back to the AI
}

void Plugin::useSeats(uint8_t count)
{
  seatCount = count > 2 ? 2 : count;
}

uint8_t Plugin::getSeatCount() const
{
  return seatCount;
}

bool Plugin::isSeatHeld(uint8_t index) const
{
  return index < 2 && seatHeld[index];
}

float Plugin::seatPosition(uint8_t index) const
{
  return index < 2 ? seatY[index] : 0.5f;
}

uint8_t Plugin::seatDirection(uint8_t index) const
{
  return index < 2 ? seatDir[index] : 0;
}

bool Plugin::takePress(uint8_t index)
{
  if (index >= 2 || seatPress[index] == 0)
    return false;

  seatPress[index]--;
  return true;
}

void Plugin::updateSeats()
{
  const unsigned long now = millis();
  for (uint8_t i = 0; i < seatCount; i++)
  {
    if (seatHeld[i] && now - seatAt[i] > SEAT_TIMEOUT_MS)
    {
      seatHeld[i] = false;
      seatTok[i] = "";
      broadcastGame();
    }
  }
}

String Plugin::getStatus() const
{
  return "";
}

char Plugin::getAxis() const
{
  return 'y';
}

bool Plugin::hasButton() const
{
  return false;
}

void Plugin::broadcastGame()
{
#ifdef ENABLE_SERVER
  String message = "{\"event\":\"game\",\"taken\":[";
  for (uint8_t i = 0; i < 2; i++)
  {
    if (i)
      message += ',';
    message += (i < seatCount && seatHeld[i]) ? "true" : "false";
  }
  message += "],\"axis\":\"";
  message += getAxis();
  message += "\",\"button\":";
  message += hasButton() ? "true" : "false";
  message += ",\"status\":\"";
  message += getStatus();
  message += "\"}";
  sendWSMessage(message);
#endif
}

bool Plugin::handleGamepad(JsonDocument &request)
{
  const char *action = request["action"];
  if (!action || seatCount == 0)
    return false;

  if (!strcmp(action, "state"))
  {
    broadcastGame();
    return true;
  }

  const char *token = request["token"];
  const int seat = request["seat"].as<int>();
  if (!token || seat < 1 || seat > (int)seatCount)
    return false;

  const uint8_t index = (uint8_t)(seat - 1);

  if (!strcmp(action, "join"))
  {
    // Free, or already ours after a reconnect. Never stealable otherwise.
    if (!seatHeld[index] || seatTok[index] == token)
    {
      seatHeld[index] = true;
      seatTok[index] = token;
      seatAt[index] = millis();
    }
    broadcastGame();
    return true;
  }

  // Everything below has to come from the seat holder.
  if (!seatHeld[index] || seatTok[index] != token)
    return false;

  if (!strcmp(action, "leave"))
  {
    seatHeld[index] = false;
    seatTok[index] = "";
    broadcastGame();
  }
  else if (!strcmp(action, "move"))
  {
    seatAt[index] = millis();
    int pos = request["pos"].as<int>();
    if (pos < 0)
      pos = 0;
    if (pos > 100)
      pos = 100;
    seatY[index] = (float)pos / 100.0f;
  }
  else if (!strcmp(action, "dir"))
  {
    // Grid games steer instead of sliding.
    seatAt[index] = millis();
    const char *dir = request["dir"];
    if (dir)
    {
      if (!strcmp(dir, "up"))
        seatDir[index] = 1;
      else if (!strcmp(dir, "right"))
        seatDir[index] = 2;
      else if (!strcmp(dir, "down"))
        seatDir[index] = 3;
      else if (!strcmp(dir, "left"))
        seatDir[index] = 4;
    }
  }
  else if (!strcmp(action, "btn"))
  {
    seatAt[index] = millis();
    if (seatPress[index] < 3) // a queue deep enough to survive one slow frame
      seatPress[index]++;
  }
  else if (!strcmp(action, "ping"))
  {
    seatAt[index] = millis();
  }

  return true;
}

const std::vector<PluginParam> &Plugin::getParams() const
{
  return params;
}

bool Plugin::setParam(const char *key, int value)
{
  for (auto &param : params)
  {
    if (strcmp(param.key, key))
      continue;

    if (value < param.min)
      value = param.min;
    if (value > param.max)
      value = param.max;

    param.value = (int16_t)value;
    onParamChanged(key, value);
    return true;
  }
  return false;
}

PluginManager::PluginManager() : nextPluginId(1)
{
}

void PluginManager::init()
{
  Screen.clear();
  std::vector<Plugin *> &allPlugins = pluginManager.getAllPlugins();

  activatePersistedPlugin();
}

void PluginManager::renderPluginId(int pluginId)
{
  if (Scheduler.isActive)
  {
    return;
  }

  Screen.clear();

  std::vector<int> digits;

  if (pluginId >= 10)
  {
    digits.push_back((pluginId - pluginId % 10) / 10);
    digits.push_back(pluginId % 10);
  }
  else
  {
    digits.push_back(pluginId);
  }

  if (pluginId >= 10)
  {
    Screen.drawNumbers(3, 6, digits, MAX_BRIGHTNESS);
  }
  else
  {
    Screen.drawNumbers(6, 6, digits, MAX_BRIGHTNESS);
  }

  unsigned long startTime = millis();
  while (millis() - startTime < 800)
  {
    yield();
#ifdef ESP32
    vTaskDelay(pdMS_TO_TICKS(10));
#else
    delay(10);
#endif
  }
}

void PluginManager::activatePersistedPlugin()
{
  std::vector<Plugin *> &allPlugins = pluginManager.getAllPlugins();
#ifdef ENABLE_STORAGE
  storage.begin("led-wall", true);
  persistedPluginId = storage.getInt("current-plugin", allPlugins.at(0)->getId());
  pluginManager.setActivePluginById(persistedPluginId);
  storage.end();
#endif
  if (!activePlugin)
  {
    pluginManager.setActivePluginById(allPlugins.at(0)->getId());
  }
}

void PluginManager::persistActivePlugin()
{
#ifdef ENABLE_STORAGE
  storage.begin("led-wall", false);
  if (activePlugin)
  {
    persistedPluginId = activePlugin->getId();
    storage.putInt("current-plugin", persistedPluginId);
  }
  storage.end();
#endif
}

int PluginManager::getPersistedPluginId()
{
  std::vector<Plugin *> &allPlugins = pluginManager.getAllPlugins();
#ifdef ENABLE_STORAGE
  storage.begin("led-wall", true);
  persistedPluginId = storage.getInt("current-plugin", allPlugins.at(0)->getId());
  storage.end();
  return persistedPluginId;
#else
  return -1;
#endif
}

int PluginManager::addPlugin(Plugin *plugin)
{

  plugin->setId(nextPluginId++);
  plugins.push_back(plugin);
  return plugin->getId();
}

void PluginManager::setActivePlugin(const char *pluginName)
{
  if (activePlugin)
  {
    activePlugin->teardown();
    activePlugin = nullptr;
  }

  for (Plugin *plugin : plugins)
  {
    if (strcmp(plugin->getName(), pluginName) == 0)
    {
      currentStatus = LOADING; // Prevent plugin loop from drawing during ID display
      activePlugin = plugin;
      renderPluginId(activePlugin->getId());
      currentStatus = NONE; // Allow plugin to start drawing
      activePlugin->setup();
      break;
    }
  }
}

void PluginManager::setActivePluginById(int pluginId)
{
  for (Plugin *plugin : plugins)
  {
    if (plugin->getId() == pluginId)
    {
      setActivePlugin(plugin->getName());
    }
  }
}

void PluginManager::setupActivePlugin()
{
  if (activePlugin)
  {
    renderPluginId(activePlugin->getId());
    activePlugin->setup();
  }
}

void PluginManager::runActivePlugin()
{
  if (activePlugin && currentStatus != UPDATE && currentStatus != LOADING &&
      currentStatus != WSBINARY)
  {
    activePlugin->loop();
  }
}

Plugin *PluginManager::getActivePlugin() const
{
  return activePlugin;
}

std::vector<Plugin *> &PluginManager::getAllPlugins()
{
  return plugins;
}

size_t PluginManager::getNumPlugins()
{
  return plugins.size();
}

void PluginManager::activateNextPlugin()
{
  if (activePlugin)
  {
    if (activePlugin->getId() <= getNumPlugins() - 1)
    {
      setActivePluginById(activePlugin->getId() + 1);
    }
    else
    {
      setActivePluginById(1);
    }
  }
  else
  {
    setActivePluginById(1);
  }
#ifdef ENABLE_SERVER
  sendInfo();
#endif
}