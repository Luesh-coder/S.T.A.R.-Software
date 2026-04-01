import { apiLog, HTTP_TIMEOUT_MS, makeBaseUrl } from "./config";
import { ApiErr } from "./types";

type HttpMethod = "GET" | "POST" | "PUT" | "DELETE";

// ─── Timeout Helper ───────────────────────────────────────────────────────────

function makeAbortSignal(ms: number): { signal: AbortSignal; cancel: () => void } {
  const controller = new AbortController();
  const id = setTimeout(() => controller.abort(), ms);
  return { signal: controller.signal, cancel: () => clearTimeout(id) };
}

// ─── Core Fetch Wrapper ───────────────────────────────────────────────────────

/**
 * Send a JSON HTTP request to the ESP32 and return the parsed response.
 * Returns an ApiErr object on network failure, timeout, or non-2xx status.
 */
export async function httpJson<T>(
  host: string,
  path: string,
  method: HttpMethod,
  body?: unknown,
  timeoutMs = HTTP_TIMEOUT_MS,
): Promise<T | ApiErr> {
  const url = `${makeBaseUrl(host)}${path}`;

  apiLog(`→ ${method} ${url}`, body !== undefined ? body : "");

  const { signal, cancel } = makeAbortSignal(timeoutMs);

  try {
    const res = await fetch(url, {
      method,
      headers: { "Content-Type": "application/json" },
      body: body !== undefined ? JSON.stringify(body) : undefined,
      signal,
    });

    const text = await res.text();
    const data = text ? JSON.parse(text) : null;

    if (!res.ok) {
      const errMsg = `HTTP ${res.status}: ${data?.error ?? text ?? "Request failed"}`;
      apiLog(`← ERROR ${url}:`, errMsg);
      return { ok: false, error: errMsg };
    }

    apiLog(`← OK ${url}:`, data);
    return data as T;
  } catch (e: any) {
    const msg =
      e?.name === "AbortError"
        ? `Request timed out — ESP32 not reachable at ${host}`
        : (e?.message ?? "Network error");
    apiLog(`← FAIL ${url}:`, msg);
    return { ok: false, error: msg };
  } finally {
    cancel();
  }
}
