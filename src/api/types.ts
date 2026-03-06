export type StarMode = "auto" | "manual";

export type StatusResponse = {
  ok: boolean;
  mode: StarMode;
  tracking: boolean;
  light?: boolean;
  // add fields as you implement them on ESP32:
  // battery?: number;
  // rssi?: number;
};

export type ApiOk = { ok: true };
export type ApiErr = { ok: false; error: string };
export type ApiResult<T> = T | ApiErr;
