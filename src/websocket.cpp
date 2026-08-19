#include "PluginManager.h"
#include "scheduler.h"

#ifdef ENABLE_SERVER

AsyncWebSocket ws("/ws");

// Live preview: clients opt in individually and we push the raw framebuffer to
// each subscriber. sendInfo() is far too heavy to repeat per frame.
//
// Subscriptions are per client on purpose: with one shared flag, a browser
// switching back to Draw would cut the stream to the phones mid-game.
static const uint8_t MAX_PREVIEW_CLIENTS = 8;
static uint32_t previewClients[MAX_PREVIEW_CLIENTS] = {0};
static unsigned long lastPreviewMillis = 0;
static const unsigned long PREVIEW_INTERVAL_MS = 50; // 20fps

void setPreviewEnabled(uint32_t clientId, bool enabled)
{
  if (clientId == 0)
    return;

  for (uint8_t i = 0; i < MAX_PREVIEW_CLIENTS; i++)
  {
    if (previewClients[i] == clientId)
    {
      if (!enabled)
        previewClients[i] = 0;
      return;
    }
  }

  if (!enabled)
    return;

  for (uint8_t i = 0; i < MAX_PREVIEW_CLIENTS; i++)
  {
    if (previewClients[i] == 0)
    {
      previewClients[i] = clientId;
      return;
    }
  }
}

void sendPreviewFrame()
{
  if (currentStatus != NONE || ws.count() == 0)
    return;

  bool anySubscriber = false;
  for (uint8_t i = 0; i < MAX_PREVIEW_CLIENTS && !anySubscriber; i++)
    anySubscriber = previewClients[i] != 0;
  if (!anySubscriber)
    return;

  unsigned long now = millis();
  if (now - lastPreviewMillis < PREVIEW_INTERVAL_MS)
    return;

  // Advance the slot even when the frame is dropped below, otherwise a skipped
  // send is followed by an immediate one and the stream arrives in bursts.
  lastPreviewMillis = now;

  // Skip rather than queue behind a slow client.
  if (!ws.availableForWriteAll())
    return;

  // Raw framebuffer: 256 bytes against ~1KB of JSON, and no String building in
  // the task that also runs the game loop.
  const uint8_t *buffer = Screen.getRenderBuffer();
  for (uint8_t i = 0; i < MAX_PREVIEW_CLIENTS; i++)
  {
    if (previewClients[i] != 0)
      ws.binary(previewClients[i], buffer, TOTAL_PIXELS);
  }
}

void sendInfo()
{
  JsonDocument jsonDocument;
  if (currentStatus == NONE)
  {
    for (int j = 0; j < ROWS * COLS; j++)
    {
      jsonDocument["data"][j] = Screen.getRenderBuffer()[j];
    }
  }

  jsonDocument["status"] = currentStatus;
  jsonDocument["plugin"] = pluginManager.getActivePlugin()->getId();
  jsonDocument["persist-plugin"] = pluginManager.getPersistedPluginId();
  jsonDocument["event"] = "info";
  jsonDocument["rotation"] = Screen.currentRotation;
  jsonDocument["brightness"] = Screen.getCurrentBrightness();
  jsonDocument["power"] = Screen.isPoweredOn();
  jsonDocument["scheduleActive"] = Scheduler.isActive;

  JsonArray scheduleArray = jsonDocument["schedule"].to<JsonArray>();
  for (const auto &item : Scheduler.schedule)
  {
    JsonObject scheduleItem = scheduleArray.add<JsonObject>();
    scheduleItem["pluginId"] = item.pluginId;
    scheduleItem["duration"] = item.duration / 1000; // Convert milliseconds to seconds
  }

  // Carry the whole control model on every info, not just on the game's own
  // broadcast: a controller already open on a phone has no other way to learn
  // that the board switched to a game steered differently.
  Plugin *active = pluginManager.getActivePlugin();
  jsonDocument["seats"] = active ? active->getSeatCount() : 0;

  if (active && active->getSeatCount() > 0)
  {
    char axis[2] = {active->getAxis(), 0};
    jsonDocument["axis"] = axis;
    jsonDocument["button"] = active->hasButton();
    jsonDocument["status"] = active->getStatus();

    JsonArray takenArray = jsonDocument["taken"].to<JsonArray>();
    takenArray.add(active->isSeatHeld(0));
    takenArray.add(active->isSeatHeld(1));
  }

  // Tunables of whatever is running now; the UI builds its sliders from these.
  JsonArray paramArray = jsonDocument["params"].to<JsonArray>();
  if (pluginManager.getActivePlugin())
  {
    for (const auto &param : pluginManager.getActivePlugin()->getParams())
    {
      JsonObject object = paramArray.add<JsonObject>();
      object["key"] = param.key;
      object["label"] = param.label;
      object["min"] = param.min;
      object["max"] = param.max;
      object["value"] = param.value;
    }
  }

  JsonArray plugins = jsonDocument["plugins"].to<JsonArray>();

  std::vector<Plugin *> &allPlugins = pluginManager.getAllPlugins();
  for (Plugin *plugin : allPlugins)
  {
    JsonObject object = plugins.add<JsonObject>();

    object["id"] = plugin->getId();
    object["name"] = plugin->getName();
  }
  String output;
  serializeJson(jsonDocument, output);
  ws.textAll(output);
  jsonDocument.clear();
}

void sendWSMessage(String &message) {
  ws.textAll(message);
}

void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    sendInfo();
  }

  if (type == WS_EVT_DISCONNECT && client)
  {
    setPreviewEnabled(client->id(), false);
  }

  if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len)
    {
      if (info->opcode == WS_BINARY && currentStatus == WSBINARY && info->len == 256)
      {
        Screen.setRenderBuffer(data, true);
      }
      else if (info->opcode == WS_TEXT)
      {
        data[len] = 0;

        JsonDocument wsRequest;
        DeserializationError error = deserializeJson(wsRequest, data);

        if (error)
        {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }
        else
        {
          pluginManager.getActivePlugin()->websocketHook(wsRequest);

          const char *event = wsRequest["event"];

          if (!strcmp(event, "plugin"))
          {
            int pluginId = wsRequest["plugin"];

            Scheduler.clearSchedule();
            pluginManager.setActivePluginById(pluginId);
            sendInfo();
          }
          else if (!strcmp(event, "persist-plugin"))
          {
            pluginManager.persistActivePlugin();
            sendInfo();
          }
          else if (!strcmp(event, "rotate"))
          {
            bool isRight = (bool)!strcmp(wsRequest["direction"], "right");
            Screen.setCurrentRotation((Screen.currentRotation + (isRight ? 1 : 3)) % 4, true);
            sendInfo();
          }
          else if (!strcmp(event, "info"))
          {
            sendInfo();
          }
          else if (!strcmp(event, "power"))
          {
            Screen.setPower(wsRequest["on"].as<bool>(), true);
            sendInfo();
          }
          else if (!strcmp(event, "gamepad"))
          {
            if (pluginManager.getActivePlugin())
              pluginManager.getActivePlugin()->handleGamepad(wsRequest);
          }
          else if (!strcmp(event, "param"))
          {
            const char *key = wsRequest["key"];
            if (key && pluginManager.getActivePlugin())
            {
              pluginManager.getActivePlugin()->setParam(key, wsRequest["value"].as<int>());
            }
          }
          else if (!strcmp(event, "preview"))
          {
            if (client)
              setPreviewEnabled(client->id(), wsRequest["enabled"].as<bool>());
          }
          else if (!strcmp(event, "brightness"))
          {
            uint8_t brightness = wsRequest["brightness"].as<uint8_t>();
            Screen.setBrightness(brightness, true);
            sendInfo();
          }
        }
      }
    }
  }
}

void initWebsocketServer(AsyncWebServer &server)
{
  server.addHandler(&ws);
  ws.onEvent(onWsEvent);
}

void cleanUpClients()
{
  ws.cleanupClients();
}

#endif
