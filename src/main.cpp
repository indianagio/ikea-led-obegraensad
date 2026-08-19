#include <Arduino.h>
#include <BfButton.h>
#include <SPI.h>

#ifdef ESP8266
/* Fix duplicate defs of HTTP_GET, HTTP_POST, ... in ESPAsyncWebServer.h */
#define WEBSERVER_H
#endif

#include <WiFiManager.h>

#ifdef ESP32
#include <ESPmDNS.h>
#endif
#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif

#include "PluginManager.h"
#include "config.h"
#include "scheduler.h"

#include "plugins/ArtNet.h"
#include "plugins/Blob.h"
#include "plugins/BreakoutPlugin.h"
#include "plugins/BubblesPlugin.h"
#include "plugins/CandlePlugin.h"
#include "plugins/CheckerboardPlugin.h"
#include "plugins/CirclePlugin.h"
#include "plugins/CometPlugin.h"
#include "plugins/DDPPlugin.h"
#include "plugins/DrawPlugin.h"
#include "plugins/DropPlugin.h"
#include "plugins/FirefliesPlugin.h"
#include "plugins/FireworkPlugin.h"
#include "plugins/FlappyPlugin.h"
#include "plugins/GameOfLifePlugin.h"
#include "plugins/InvadersPlugin.h"
#include "plugins/LinesPlugin.h"
#include "plugins/MatrixRainPlugin.h"
#include "plugins/MeteorShowerPlugin.h"
#include "plugins/PongClockPlugin.h"
#include "plugins/PongPlugin.h"
#include "plugins/RadarPlugin.h"
#include "plugins/RainPlugin.h"
#include "plugins/ScanlinesPlugin.h"
#include "plugins/SandPlugin.h"
#include "plugins/SnakePlugin.h"
#include "plugins/SparkleFieldPlugin.h"
#include "plugins/SpiralPlugin.h"
#include "plugins/StarsPlugin.h"
#include "plugins/TickingClockPlugin.h"
#include "plugins/WaveBarsPlugin.h"
#include "plugins/WavePlugin.h"

#ifdef ENABLE_SERVER
#include "plugins/AnimationPlugin.h"
#include "plugins/BigClockPlugin.h"
#include "plugins/ClockPlugin.h"
#include "plugins/WeatherPlugin.h"
#endif

#include "asyncwebserver.h"
#include "messages.h"
#include "ota.h"
#include "screen.h"
#include "secrets.h"
#include "websocket.h"

BfButton btn(BfButton::STANDALONE_DIGITAL, PIN_BUTTON, true, LOW);

unsigned long previousMillis = 0;
unsigned long interval = 30000;

PluginManager pluginManager;
#ifdef ESP32
DRAM_ATTR volatile SYSTEM_STATUS currentStatus = NONE;
#else
volatile SYSTEM_STATUS currentStatus = NONE;
#endif
WiFiManager wifiManager;

// Long-hold handling lives here rather than in ButtonFever: that library takes
// a single press-for threshold, and it is already spent on the 1s power toggle.
namespace
{
constexpr unsigned long HOLD_POWER_MS = 1000;  // ButtonFever's threshold
constexpr unsigned long HOLD_HINT_MS = 3000;   // countdown becomes visible
constexpr unsigned long HOLD_WIFI_MS = 10000;  // credentials wiped

bool holdPending = false;        // a long press started and has not been released
bool holdShowingHint = false;    // the panel is taken over by the countdown
bool holdPowerBeforeHint = true; // power state to restore if the hold is aborted
unsigned long holdStartedAt = 0;
} // namespace

// SNTP keeps the pointer it is handed rather than copying the string
// (sntp_servers[idx].name = server), so the server name has to outlive the
// configTzTime() call. Passing a temporary String's c_str() leaves it pointing
// at freed heap and the clock never syncs.
String ntpServer;
String tzInfo;

unsigned long lastConnectionAttempt = 0;
const unsigned long connectionInterval = 10000;
unsigned long reconnectionBackoff = 5000;            // Start with 5 seconds
const unsigned long maxReconnectionBackoff = 300000; // Max 5 minutes
uint8_t reconnectionAttempts = 0;

void connectToWiFi()
{
  // if a WiFi setup AP was started, reboot is required to clear routes
  bool wifiWebServerStarted = false;
  wifiManager.setWebServerCallback([&wifiWebServerStarted]() { wifiWebServerStarted = true; });

  wifiManager.setHostname(WIFI_HOSTNAME);

#if defined(IP_ADDRESS) && defined(GWY) && defined(SUBNET) && defined(DNS1)
  auto ip = IPAddress();
  ip.fromString(IP_ADDRESS);

  auto gwy = IPAddress();
  gwy.fromString(GWY);

  auto subnet = IPAddress();
  subnet.fromString(SUBNET);

  auto dns = IPAddress();
  dns.fromString(DNS1);

  wifiManager.setSTAStaticIPConfig(ip, gwy, subnet, dns);
#endif

  wifiManager.setConnectRetries(10);
  wifiManager.setConnectTimeout(10);
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.autoConnect(WIFI_MANAGER_SSID);

#ifdef ESP32
  if (MDNS.begin(WIFI_HOSTNAME))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.setInstanceName(WIFI_HOSTNAME);
  }
  else
  {
    Serial.println("Could not start mDNS!");
  }
#endif

  if (wifiWebServerStarted)
  {
    // Reboot required, otherwise wifiManager server interferes with our server
    Serial.println("Done running WiFi Manager webserver - rebooting");
    ESP.restart();
  }

  lastConnectionAttempt = millis();
}

void pressHandler(BfButton *btn, BfButton::press_pattern_t pattern)
{
  switch (pattern)
  {
  case BfButton::SINGLE_PRESS:
    if (currentStatus != LOADING)
    {
      Scheduler.clearSchedule();
      pluginManager.activateNextPlugin();
    }
    break;

  case BfButton::DOUBLE_PRESS:
    // Back to the mode you marked as default.
    if (currentStatus != LOADING)
    {
      Scheduler.clearSchedule();
      pluginManager.activatePersistedPlugin();
    }
    break;

  case BfButton::LONG_PRESS:
    // Don't act yet: the user may be on their way to the 10s WiFi reset. The
    // power toggle happens on release, which also lets the panel stay lit to
    // show the countdown.
    if (currentStatus != LOADING)
    {
      holdPending = true;
      holdShowingHint = false;
      holdStartedAt = millis() - HOLD_POWER_MS;
    }
    break;
  }
}

// Draws the "keep holding to forget WiFi" countdown and fires the reset.
void updateLongHold()
{
  if (!holdPending)
    return;

  const bool stillDown = digitalRead(PIN_BUTTON) == LOW;
  const unsigned long held = millis() - holdStartedAt;

  if (!stillDown)
  {
    holdPending = false;

    if (holdShowingHint)
    {
      // Released mid-countdown: treat it as an abort, not as a power toggle.
      holdShowingHint = false;
      currentStatus = NONE;
      Screen.setPower(holdPowerBeforeHint);
      Screen.clear();
    }
    else
    {
      Screen.setPower(!Screen.isPoweredOn(), true);
#ifdef ENABLE_SERVER
      sendInfo();
#endif
    }
    return;
  }

  if (held >= HOLD_WIFI_MS)
  {
    holdPending = false;
    holdShowingHint = false;

    Screen.clear();
    Screen.drawRectangle(0, 0, COLS, ROWS, true, 1, MAX_BRIGHTNESS);

    Serial.println("Button held: clearing WiFi credentials and restarting");
    wifiManager.resetSettings();
    delay(400); // let the flash write settle and the flash of light be seen
    ESP.restart();
    return;
  }

  if (held < HOLD_HINT_MS)
    return;

  if (!holdShowingHint)
  {
    holdShowingHint = true;
    holdPowerBeforeHint = Screen.isPoweredOn();
    // Take the panel off the plugin and make sure it is lit, or a countdown on
    // a switched-off display would be invisible.
    currentStatus = LOADING;
    Screen.setPower(true);
  }

  const float progress = (float)(held - HOLD_HINT_MS) / (float)(HOLD_WIFI_MS - HOLD_HINT_MS);
  const int filled = (int)(progress * (float)COLS + 0.5f);

  Screen.clear();
  for (int x = 0; x < COLS; x++)
  {
    const uint8_t shade = x < filled ? MAX_BRIGHTNESS : 25;
    Screen.setPixel(x, 7, 1, shade);
    Screen.setPixel(x, 8, 1, shade);
  }
}

void baseSetup()
{
  Serial.begin(115200);

  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);

#if !defined(ESP32) && !defined(ESP8266)
  Screen.setup();
#endif

  // Initialize configuration system (always safe)
  config.begin();

// server
#ifdef ENABLE_SERVER
  connectToWiFi();

  // set time server using config values
  tzInfo = config.getTzInfo();
  ntpServer = config.getNtpServer();
  configTzTime(tzInfo.c_str(), ntpServer.c_str());

  initOTA(server);
  initWebsocketServer(server);
  initWebServer();
#endif

  pluginManager.addPlugin(new DrawPlugin());
  pluginManager.addPlugin(new BreakoutPlugin());
  pluginManager.addPlugin(new SnakePlugin());
  pluginManager.addPlugin(new GameOfLifePlugin());
  pluginManager.addPlugin(new StarsPlugin());
  pluginManager.addPlugin(new LinesPlugin());
  pluginManager.addPlugin(new CirclePlugin());
  pluginManager.addPlugin(new RainPlugin());
  pluginManager.addPlugin(new MatrixRainPlugin());
  pluginManager.addPlugin(new FireworkPlugin());
  pluginManager.addPlugin(new BlobPlugin());
  pluginManager.addPlugin(new SpiralPlugin());
  pluginManager.addPlugin(new WavePlugin());
  pluginManager.addPlugin(new CheckerboardPlugin());
  pluginManager.addPlugin(new RadarPlugin());
  pluginManager.addPlugin(new BubblesPlugin());
  pluginManager.addPlugin(new CometPlugin());
  pluginManager.addPlugin(new FirefliesPlugin());
  pluginManager.addPlugin(new MeteorShowerPlugin());
  pluginManager.addPlugin(new ScanlinesPlugin());
  pluginManager.addPlugin(new SparkleFieldPlugin());
  pluginManager.addPlugin(new WaveBarsPlugin());

#ifdef ENABLE_SERVER
  pluginManager.addPlugin(new BigClockPlugin());
  pluginManager.addPlugin(new ClockPlugin());
  pluginManager.addPlugin(new PongClockPlugin());
  pluginManager.addPlugin(new TickingClockPlugin());
  pluginManager.addPlugin(new WeatherPlugin());
  pluginManager.addPlugin(new AnimationPlugin());
  pluginManager.addPlugin(new DDPPlugin());
  pluginManager.addPlugin(new ArtNetPlugin());
#endif

  pluginManager.addPlugin(new CandlePlugin());
  pluginManager.addPlugin(new SandPlugin());
  pluginManager.addPlugin(new DropPlugin());
  pluginManager.addPlugin(new PongPlugin());
  pluginManager.addPlugin(new FlappyPlugin());
  pluginManager.addPlugin(new InvadersPlugin());

  Screen.clear();
  pluginManager.init();
  Scheduler.init();

  btn.onPress(pressHandler).onDoublePress(pressHandler).onPressFor(pressHandler, 1000);
}

#ifdef ESP32
TaskHandle_t screenDrawingTaskHandle = NULL;

void screenDrawingTask(void *parameter)
{
  Screen.setup();
  for (;;)
  {
    // A blank panel has nothing to animate, so let the plugin idle.
    if (Screen.isPoweredOn())
      pluginManager.runActivePlugin();

    // The live preview is driven from here rather than from loop(), which can
    // be held up by other work. It rate-limits itself.
#ifdef ENABLE_SERVER
    sendPreviewFrame();
#endif
    vTaskDelay(1);
  }
}

void setup()
{
  baseSetup();
  xTaskCreatePinnedToCore(screenDrawingTask,
                          "screenDrawingTask",
                          10000,
                          NULL,
                          1,
                          &screenDrawingTaskHandle,
                          0);
}
#endif
#ifdef ESP8266
void screenDrawingTask()
{
  Screen.setup();
  pluginManager.runActivePlugin();
  yield();
}

void setup()
{
  baseSetup();
  Scheduler.start();
}
#endif

void loop()
{
  static uint8_t taskCounter = 0;

  btn.read();
  updateLongHold();

#ifdef ENABLE_SERVER
  ElegantOTA.loop();
#endif

#if !defined(ESP32) && !defined(ESP8266)
  pluginManager.runActivePlugin();
#endif

  if (currentStatus == NONE)
  {
    Scheduler.update();

    if ((taskCounter & 0x03) == 0)
    {
      Messages.scrollMessageEveryMinute();
    }
  }

  // Check WiFi less frequently with exponential backoff
  if (WiFi.status() != WL_CONNECTED)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - lastConnectionAttempt >= reconnectionBackoff)
    {
      Serial.println("WiFi disconnected, attempting reconnection...");
      connectToWiFi();

      // Exponential backoff: double the wait time, up to max
      reconnectionAttempts++;
      reconnectionBackoff = min(reconnectionBackoff * 2, maxReconnectionBackoff);
    }
  }
  else
  {
    if (reconnectionAttempts > 0)
    {
      Serial.println("WiFi reconnected successfully");
      reconnectionAttempts = 0;
      reconnectionBackoff = 5000;
    }
  }

  taskCounter++;
  if (taskCounter > 16)
  {
    taskCounter = 0;
  }

#ifdef ENABLE_SERVER
  cleanUpClients();
#endif
#ifdef ESP32
  vTaskDelay(1);
#else
  delay(1);
#endif
}
