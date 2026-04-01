// ─── Core Domain Types ───────────────────────────────────────────────────────

export type StarMode = "auto" | "manual";

export type StatusResponse = {
  mode: StarMode;
  tracking: boolean;
  light: boolean;
  // Future fields (uncomment as you implement on ESP32):
  // battery?: number;
  // rssi?: number;
  // target_id?: number;
};

// ─── API Response Wrappers ────────────────────────────────────────────────────

export type ApiOk = { ok: true };
export type ApiErr = { ok: false; error: string };
export type ApiResult<T> = T | ApiErr;

export function isApiErr(r: unknown): r is ApiErr {
  return (
    typeof r === "object" &&
    r !== null &&
    "ok" in r &&
    (r as ApiErr).ok === false
  );
}

// ─── Connection State ─────────────────────────────────────────────────────────

/** Overall reachability state of the ESP32 */
export type ConnectionState = "unknown" | "online" | "offline";
