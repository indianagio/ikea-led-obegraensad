import { type Component, createEffect, createMemo, onCleanup, Show } from "solid-js";

import { Layout } from "./components/layout/layout";
import Sidebar from "./components/layout/sidebar";
import { LedMatrix } from "./components/led-matrix";
import { ScreenInfo } from "./components/screen-info";
import { useStore } from "./contexts/store";
import { useToast } from "./contexts/toast";
import { loadImageAndGetDataArray, rotateArray } from "./helpers";


export const App: Component = () => {
  const [store, actions] = useStore();
  const { toast } = useToast();

  const rotatedMatrix = createMemo(() => rotateArray(store.indexMatrix, store.rotation));

  const wsMessage = (
    event:
      | "persist"
      | "load"
      | "clear"
      | "plugin"
      | "screen"
      | "led"
      | "persist-plugin"
      | "artnet"
      | "brightness"
      | "goldelay"
      | "param"
      | "preview"
      | "power",
    data?: Record<string, string | number> | { data: number[] },
  ) =>
    actions.send(
      JSON.stringify({
        event,
        ...data,
      }),
    );

  const handleRotate = (turnRight = false) => {
    const currentRotation = store.rotation || 0;
    actions.setRotation((currentRotation + (turnRight ? 1 : -1) + 4) % 4);
    actions.send(
      JSON.stringify({
        event: "rotate",
        direction: turnRight ? "right" : "left",
      }),
    );
  };

  const handleLoadImage = () => {
    loadImageAndGetDataArray((data) => {
      actions.setLeds(store.indexMatrix.map((index) => (data[index] ? 255 : 0)));
      wsMessage("screen", { data });
    });
  };

  const handleClear = () => {
    actions?.setLeds([...new Array(256).fill(0)]);
    wsMessage("clear");
    toast(`Canvas cleared`, 1000);
  };

  const handlePersist = () => {
    wsMessage("persist");
    toast(`Saved current state`, 1500);
  };

  const handleLoad = () => {
    wsMessage("load");
    toast(`Saved state loaded`, 1500);
  };

  const handlePluginChange = (pluginId: number) => {
    wsMessage("plugin", { plugin: pluginId });
    toast("Mode changed", 1000);
  };

  const handleBrightnessChange = (value: number, shouldSend = false) => {
    actions?.setBrightness(value);
    if (shouldSend) {
      wsMessage("brightness", { brightness: value });
    }
  };

  const handleArtnetUniverseChange = (value: number, shouldSend = false) => {
    actions?.setArtnetUniverse(value);
    if (shouldSend) {
      wsMessage("artnet", { universe: value });
    }
  };

  const handleGOLDelayChange = (value: number, shouldSend = false) => {
    actions?.setGOLDelay(value);
    if (shouldSend) {
      wsMessage("goldelay", { delay: value });
    }
  };

  // The board only streams frames while somebody is watching, so subscribe for
  // every mode that renders itself and unsubscribe for Draw.
  const isPreviewMode = createMemo(() => store.plugin !== 1 && !store.isActiveScheduler);

  createEffect(() => {
    wsMessage("preview", { enabled: isPreviewMode() ? 1 : 0 });
  });

  onCleanup(() => wsMessage("preview", { enabled: 0 }));

  const activePluginName = createMemo(
    () => store.plugins.find((plugin) => plugin.id === store.plugin)?.name ?? "A plugin",
  );

  const handleParamChange = (key: string, value: number, shouldSend = false) => {
    actions?.setParamValue(key, value);
    if (shouldSend) {
      wsMessage("param", { key, value });
    }
  };

  const handlePowerToggle = () => {
    const next = !store.power;
    actions?.setPower(next);
    wsMessage("power", { on: next ? 1 : 0 });
    toast(next ? "Display on" : "Display off", 1200);
  };

  const handlePersistPlugin = () => {
    wsMessage("persist-plugin");
    toast(`Current mode set as default`, 1500);
  };

  const renderLedMatrix = () => (
    <div class="grid p-8 h-full justify-center items-center sm:p-4 sm:m-0">
      <Show
        when={!store.isActiveScheduler}
        fallback={
          <ScreenInfo>
            <h2 class="text-4xl">Scheduler is running</h2>
          </ScreenInfo>
        }
      >
        <div class="flex flex-col items-center gap-6">
          <LedMatrix
            disabled={store.plugin !== 1}
            data={store.leds || []}
            indexData={rotatedMatrix()}
            brightness={store.brightness ?? 255}
            onSetLed={(data) => {
              wsMessage("led", data);
            }}
            onSetMatrix={(data) => {
              actions?.setLeds([...data]);
            }}
          />

          <Show when={store.plugin !== 1}>
            <p class="text-sm text-gray-400">
              Live preview &mdash; {activePluginName()} is running
            </p>
          </Show>

          <Show when={store.plugin === 1}>
            <div class="lg:hidden w-full max-w-100 sm:max-w-125">
              <div class="grid grid-cols-4 gap-2">
                <button
                  type="button"
                  onClick={handleLoadImage}
                  class="flex flex-col items-center justify-center gap-1 bg-gray-700 text-white border-0 p-2 cursor-pointer font-semibold hover:opacity-80 active:-translate-y-px transition-all rounded text-xs"
                >
                  <i class="fa-solid fa-file-import text-base" />
                  <span>Import</span>
                </button>
                <button
                  type="button"
                  onClick={handleClear}
                  class="flex flex-col items-center justify-center gap-1 bg-gray-700 text-white border-0 p-2 cursor-pointer font-semibold hover:opacity-80 active:-translate-y-px transition-all rounded hover:bg-red-600 text-xs"
                >
                  <i class="fa-solid fa-trash text-base" />
                  <span>Clear</span>
                </button>
                <button
                  type="button"
                  onClick={handlePersist}
                  class="flex flex-col items-center justify-center gap-1 bg-gray-700 text-white border-0 p-2 cursor-pointer font-semibold hover:opacity-80 active:-translate-y-px transition-all rounded text-xs"
                >
                  <i class="fa-solid fa-floppy-disk text-base" />
                  <span>Save</span>
                </button>
                <button
                  type="button"
                  onClick={handleLoad}
                  class="flex flex-col items-center justify-center gap-1 bg-gray-700 text-white border-0 p-2 cursor-pointer font-semibold hover:opacity-80 active:-translate-y-px transition-all rounded text-xs"
                >
                  <i class="fa-solid fa-refresh text-base" />
                  <span>Load</span>
                </button>
              </div>
            </div>
          </Show>
        </div>
      </Show>
    </div>
  );

  return (
    <Layout
      content={renderLedMatrix()}
      sidebar={
        <Sidebar
          onRotate={handleRotate}
          onLoadImage={handleLoadImage}
          onClear={handleClear}
          onPersist={handlePersist}
          onLoad={handleLoad}
          onPluginChange={handlePluginChange}
          onBrightnessChange={handleBrightnessChange}
          onArtnetChange={handleArtnetUniverseChange}
          onGOLDelayChange={handleGOLDelayChange}
          onParamChange={handleParamChange}
          onPersistPlugin={handlePersistPlugin}
          onPowerToggle={handlePowerToggle}
        />
      }
    />
  );
};
