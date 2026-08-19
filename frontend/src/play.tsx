import { type Component, createEffect, createSignal, onCleanup, onMount, Show } from "solid-js";

import { LedMatrix } from "./components/led-matrix";
import { useStore } from "./contexts/store";
import { rotateArray } from "./helpers";

// One token per device, kept across reloads so a phone that locks its screen
// and reconnects gets its seat back instead of losing it to the board.
const getToken = () => {
  const key = "play-token";
  let token = localStorage.getItem(key);
  if (!token) {
    token = Math.random().toString(36).slice(2, 12);
    localStorage.setItem(key, token);
  }
  return token;
};

const DPAD_CLASS =
  "aspect-square rounded-2xl bg-gray-700 text-white text-2xl border-0 cursor-pointer active:bg-gray-600 flex items-center justify-center";

export const Play: Component = () => {
  const [store, actions] = useStore();
  const [seat, setSeat] = createSignal(0);
  const [showBoard, setShowBoard] = createSignal(true);
  const token = getToken();

  let padRef: HTMLDivElement | undefined;
  let lastSent = -1;
  let pendingPos = -1;
  let swipeFrom: { x: number; y: number } | null = null;

  const isDpad = () => store.game.axis === "d";
  const isSlide = () => store.game.axis === "x" || store.game.axis === "y";
  const hasButton = () => store.game.button;

  const press = () => send({ action: "btn", seat: seat() });

  const steer = (dir: "up" | "down" | "left" | "right") =>
    send({ action: "dir", seat: seat(), dir });

  const send = (payload: Record<string, string | number>) =>
    actions.send(JSON.stringify({ event: "gamepad", token, ...payload }));

  const gameName = () => store.plugins.find((p) => p.id === store.plugin)?.name ?? "Game";
  const seatCount = () => store.seats ?? 0;
  const takenByOther = (n: number) => !!store.game.taken[n - 1] && seat() !== n;
  const seatLabel = (n: number) =>
    seatCount() === 1 ? "Play" : `Player ${n} (${n === 1 ? "left" : "right"})`;

  const join = (n: number) => {
    setSeat(n);
    send({ action: "join", seat: n });
  };

  const leave = () => {
    if (seat()) send({ action: "leave", seat: seat() });
    setSeat(0);
  };

  // Pong's paddle runs up the side, Breakout's runs along the bottom: the board
  // tells us which axis its controller should read.
  // Grid games steer; a swipe of more than a few pixels counts as one press and
  // re-arms from where it ended, so a long drag can chain turns.
  const swipe = (clientX: number, clientY: number) => {
    if (!swipeFrom) return;
    const dx = clientX - swipeFrom.x;
    const dy = clientY - swipeFrom.y;
    if (Math.abs(dx) < 24 && Math.abs(dy) < 24) return;

    steer(Math.abs(dx) > Math.abs(dy) ? (dx > 0 ? "right" : "left") : dy > 0 ? "down" : "up");
    swipeFrom = { x: clientX, y: clientY };
  };

  const movePaddle = (clientX: number, clientY: number) => {
    if (!padRef || !seat()) return;
    const rect = padRef.getBoundingClientRect();
    const raw =
      store.game.axis === "x"
        ? ((clientX - rect.left) / rect.width) * 100
        : ((clientY - rect.top) / rect.height) * 100;
    // Only record it here. A phone fires pointermove far faster than the game
    // steps, and one websocket message per event just gives the board more JSON
    // to parse for the same result.
    pendingPos = Math.max(0, Math.min(100, Math.round(raw)));
  };

  onMount(() => {
    send({ action: "state" });

    // Flush the latest position on a fixed cadence, matched to the game step.
    const flush = setInterval(() => {
      if (!seat() || pendingPos < 0 || pendingPos === lastSent) return;
      lastSent = pendingPos;
      send({ action: "move", seat: seat(), pos: pendingPos });
    }, 30);
    onCleanup(() => clearInterval(flush));

    // The board drops a silent seat after 9s, so keep it warm.
    const beat = setInterval(() => {
      if (seat()) send({ action: "ping", seat: seat() });
    }, 2500);
    onCleanup(() => clearInterval(beat));
  });

  createEffect(() => {
    actions.send(JSON.stringify({ event: "preview", enabled: showBoard() ? 1 : 0 }));
  });

  // A seat belongs to one plugin. When the board switches game the old claim is
  // meaningless, so drop it and make the player pick a side again.
  let knownPlugin = -1;
  createEffect(() => {
    const current = store.plugin;
    if (knownPlugin !== -1 && current !== knownPlugin && seat() !== 0) {
      setSeat(0);
    }
    knownPlugin = current;
  });

  // Switching to a plugin that takes no players makes this page meaningless.
  createEffect(() => {
    if (seatCount() === 0 && seat() !== 0) setSeat(0);
  });

  onCleanup(() => {
    leave();
    actions.send(JSON.stringify({ event: "preview", enabled: 0 }));
  });

  return (
    <div class="h-full flex flex-col bg-gray-900 text-white select-none">
      <div class="flex items-center justify-between px-4 py-3 shrink-0">
        <a href="#/" class="text-gray-400 text-sm" onClick={leave}>
          <i class="fa-solid fa-arrow-left mr-2" />
          Back
        </a>
        <div class="font-mono text-lg tracking-wide">{store.game.status}</div>
        <button
          type="button"
          class="text-gray-400 text-sm bg-transparent border-0 cursor-pointer"
          onClick={() => setShowBoard(!showBoard())}
        >
          <i class={`fa-solid ${showBoard() ? "fa-eye" : "fa-eye-slash"} mr-2`} />
          Board
        </button>
      </div>

      <Show when={showBoard()}>
        <div class="px-6 pb-3 shrink-0 max-w-70 mx-auto w-full">
          <LedMatrix
            disabled
            data={store.leds || []}
            indexData={rotateArray(store.indexMatrix, store.rotation)}
            brightness={store.brightness ?? 255}
          />
        </div>
      </Show>

      <Show
        when={seatCount() > 0}
        fallback={
          <div class="flex-1 flex items-center justify-center p-6 text-center text-gray-400">
            <div>
              <p class="text-lg">{gameName()} takes no players.</p>
              <p class="text-sm mt-2">Switch to Pong or Breakout to play.</p>
            </div>
          </div>
        }
      >
        <Show
          when={seat() !== 0}
          fallback={
            <div class="flex-1 flex flex-col justify-center gap-4 p-6">
              <p class="text-center text-gray-400">{gameName()}</p>
              {Array.from({ length: seatCount() }, (_, i) => i + 1).map((n) => (
                <button
                  type="button"
                  disabled={takenByOther(n)}
                  onClick={() => join(n)}
                  class="py-8 text-2xl font-semibold rounded-2xl border-0 cursor-pointer bg-gray-700 text-white disabled:opacity-40"
                >
                  {seatLabel(n)}
                  <Show when={takenByOther(n)}>
                    <div class="text-sm font-normal mt-1">taken</div>
                  </Show>
                </button>
              ))}
              <p class="text-center text-gray-500 text-sm">
                An empty side is played by the board.
              </p>
            </div>
          }
        >
          <div
            ref={padRef}
            class="flex-1 m-4 rounded-2xl bg-gray-800 border-2 border-dashed border-gray-600 flex items-center justify-center touch-none"
            onPointerDown={(e) => {
              e.currentTarget.setPointerCapture(e.pointerId);
              if (isDpad()) swipeFrom = { x: e.clientX, y: e.clientY };
              else if (isSlide()) movePaddle(e.clientX, e.clientY);
              else if (hasButton()) press(); // button-only game: tap anywhere
            }}
            onPointerUp={() => {
              swipeFrom = null;
            }}
            onPointerMove={(e) => {
              if (!(e.buttons || e.pointerType === "touch")) return;
              if (isDpad()) swipe(e.clientX, e.clientY);
              else if (isSlide()) movePaddle(e.clientX, e.clientY);
            }}
          >
            <Show
              when={isDpad()}
              fallback={
                <div class="text-center text-gray-500">
                  <div class="text-lg">
                    <Show when={seatCount() > 1} fallback={gameName()}>
                      You are Player {seat()}
                    </Show>
                  </div>
                  <div class="text-sm mt-1">
                    <Show when={isSlide()} fallback="Tap anywhere">
                      Slide {store.game.axis === "x" ? "left and right" : "up and down"}
                    </Show>
                  </div>
                </div>
              }
            >
              <div class="grid grid-cols-3 grid-rows-3 gap-3 w-60 max-w-full">
                <div />
                <button type="button" class={DPAD_CLASS} onPointerDown={() => steer("up")}>
                  <i class="fa-solid fa-chevron-up" />
                </button>
                <div />
                <button type="button" class={DPAD_CLASS} onPointerDown={() => steer("left")}>
                  <i class="fa-solid fa-chevron-left" />
                </button>
                <div class="flex items-center justify-center text-gray-600 text-xs">swipe</div>
                <button type="button" class={DPAD_CLASS} onPointerDown={() => steer("right")}>
                  <i class="fa-solid fa-chevron-right" />
                </button>
                <div />
                <button type="button" class={DPAD_CLASS} onPointerDown={() => steer("down")}>
                  <i class="fa-solid fa-chevron-down" />
                </button>
                <div />
              </div>
            </Show>
          </div>
          <Show when={hasButton() && !isDpad() && isSlide()}>
            <button
              type="button"
              onPointerDown={press}
              class="mx-4 mb-3 py-6 rounded-2xl bg-gray-600 text-white text-xl font-semibold border-0 cursor-pointer active:bg-gray-500 shrink-0 touch-none"
            >
              Fire
            </button>
          </Show>

          <button
            type="button"
            onClick={leave}
            class="mx-4 mb-4 py-3 rounded-xl bg-gray-700 text-white border-0 cursor-pointer shrink-0"
          >
            Leave seat
          </button>
        </Show>
      </Show>
    </div>
  );
};
