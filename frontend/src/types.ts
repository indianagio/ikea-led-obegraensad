export enum SYSTEM_STATUS {
  NONE = "draw",
  WSBINARY = "wsbinary",
  // SYSTEM
  UPDATE = "update",
  LOADING = "loading",
}

export interface ScheduleItem {
  pluginId: number;
  duration: number;
}

export interface GameState {
  taken: boolean[];
  axis: "x" | "y" | "d" | "b"; // slide horizontally/vertically, d-pad, or button only
  button: boolean;             // game also wants an action button
  status: string;
}

export interface PluginParam {
  key: string;
  label: string;
  min: number;
  max: number;
  value: number;
}

export interface StoreActions {
  setIsActiveScheduler: (isActive: boolean) => void;
  setRotation: (rotation: number) => void;
  setPlugins: (plugins: []) => void;
  setPlugin: (plugin: number) => void;
  setBrightness: (brightness: number) => void;
  setPower: (power: boolean) => void;
  setIndexMatrix: (indexMatrix: number[]) => void;
  setLeds: (leds: number[]) => void;
  setSystemStatus: (systemStatus: SYSTEM_STATUS) => void;
  setSchedule: (items: ScheduleItem[]) => void;
  setArtnetUniverse: (artnetUniverse: number) => void;
  setGOLDelay: (GOLDelay: number) => void;
  setGame: (state: GameState) => void;
  setSeats: (seats: number) => void;
  setParams: (params: PluginParam[]) => void;
  setParamValue: (key: string, value: number) => void;
  send: (message: string | ArrayBuffer) => void;
}

export interface Store {
  isActiveScheduler: boolean;
  rotation: number;
  brightness: number;
  power: boolean;
  indexMatrix: number[];
  leds: number[];
  plugins: { id: number; name: string }[];
  plugin: number;
  artnetUniverse: number;
  GOLDelay: number;
  params: PluginParam[];
  game: GameState;
  seats: number;
  systemStatus: SYSTEM_STATUS;
  connectionState: () => number;
  connectionStatus?: string;
  schedule: ScheduleItem[];
}

export interface IToastContext {
  toast: (text: string, duration: number) => void;
}
