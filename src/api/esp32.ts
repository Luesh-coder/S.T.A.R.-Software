import { httpJson } from "./http";
import { ApiOk, ApiResult, StarMode, StatusResponse } from "./types";

// ─── ESP32 REST Endpoints ─────────────────────────────────────────────────────
//
// Each function maps 1-to-1 to a route the ESP32 HTTP server must expose.
// All requests are JSON over HTTP on port 80.

const PATHS = {
  status:    "/api/status",    // GET  → StatusResponse
  mode:      "/api/mode",      // POST { mode: "auto"|"manual" }
  tracking:  "/api/tracking",  // POST { enabled: bool }
  newTarget: "/api/target/new",// POST {}
  light:     "/api/light",     // POST { enabled: bool }
} as const;

// ─── API Functions ────────────────────────────────────────────────────────────

/** Poll the ESP32 for its current state. */
export function getStatus(host: string): Promise<ApiResult<StatusResponse>> {
  return httpJson<StatusResponse>(host, PATHS.status, "GET");
}

/** Switch between "auto" (tracking) and "manual" (D-pad) mode. */
export function setMode(host: string, mode: StarMode): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATHS.mode, "POST", { mode });
}

/** Enable or disable the subject-tracking algorithm. */
export function setTracking(host: string, enabled: boolean): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATHS.tracking, "POST", { enabled });
}

/** Tell the gimbal to lock onto a new target in frame. */
export function newTarget(host: string): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATHS.newTarget, "POST", {});
}

/** Turn the spotlight on or off. */
export function setLight(host: string, enabled: boolean): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATHS.light, "POST", { enabled });
}
