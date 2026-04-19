// ─── Network Config ───────────────────────────────────────────────────────────

/** Default IP of the ESP32 when it runs as a Wi-Fi Access Point */
export const DEFAULT_ESP32_HOST = "192.168.4.1";

export const HTTP_PROTOCOL: "http" | "https" = "http";

/** WebSocket port exposed by the ESP32 */
export const WS_PORT = 81;

/** How long (ms) before an HTTP request is considered timed out */
export const HTTP_TIMEOUT_MS = 3000;

/** How often (ms) to auto-poll /api/status when connected */
export const POLL_INTERVAL_MS = 3000;

// ─── URL Builders ─────────────────────────────────────────────────────────────

export function makeBaseUrl(host: string): string {
  return `${HTTP_PROTOCOL}://${host}`;
}

export function makeWsUrl(host: string): string {
  const scheme = HTTP_PROTOCOL === "https" ? "wss" : "ws";
  return `${scheme}://${host}:${WS_PORT}/`;
}

// ─── Debug ────────────────────────────────────────────────────────────────────

/** Set to false to silence API logs in production */
export const API_DEBUG = true;

export function apiLog(...args: unknown[]) {
  if (API_DEBUG) console.log("[API]", ...args);
}
