import { httpJson } from "./http";
import { ApiOk, ApiResult, StarMode, StatusResponse } from "./types";

// These paths are what you should implement on the ESP32 server:
const PATH_STATUS = "/api/status";
const PATH_MODE = "/api/mode";
const PATH_TRACKING = "/api/tracking";
const PATH_NEW_TARGET = "/api/target/new";
const PATH_LIGHT = "/api/light";

export async function getStatus(
  host: string,
): Promise<ApiResult<StatusResponse>> {
  return httpJson<StatusResponse>(host, PATH_STATUS, "GET");
}

export async function setMode(
  host: string,
  mode: StarMode,
): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATH_MODE, "POST", { mode });
}

export async function setTracking(
  host: string,
  enabled: boolean,
): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATH_TRACKING, "POST", { enabled });
}

export async function newTarget(host: string): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATH_NEW_TARGET, "POST", {});
}

export async function setLight(
  host: string,
  enabled: boolean,
): Promise<ApiResult<ApiOk>> {
  return httpJson<ApiOk>(host, PATH_LIGHT, "POST", { enabled });
}
