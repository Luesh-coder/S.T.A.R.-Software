export const DEFAULT_ESP32_HOST = "192.168.4.1"; // ESP32 AP default gateway IP
export const DEFAULT_PROTOCOL: "http" | "https" = "http";

export const REST_PORT = 80; // usually not needed explicitly
export const WS_PORT = 80;

export function makeBaseUrl(
  host: string,
  protocol: "http" | "https" = DEFAULT_PROTOCOL,
) {
  return `${protocol}://${host}`;
}

export function makeWsUrl(host: string, path = "/ws/manual") {
  // If you ever use https, WS becomes wss:
  const scheme = DEFAULT_PROTOCOL === "https" ? "wss" : "ws";
  return `${scheme}://${host}${path}`;
}
