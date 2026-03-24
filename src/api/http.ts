import { makeBaseUrl } from "./config";
import { ApiErr } from "./types";

type HttpMethod = "GET" | "POST" | "PUT" | "PATCH" | "DELETE";

function timeoutSignal(ms: number) {
  const controller = new AbortController();
  const id = setTimeout(() => controller.abort(), ms);
  return { signal: controller.signal, cancel: () => clearTimeout(id) };
}

export async function httpJson<T>(
  host: string,
  path: string,
  method: HttpMethod,
  body?: unknown,
  timeoutMs = 2500,
): Promise<T | ApiErr> {
  const base = makeBaseUrl(host);
  const url = `${base}${path}`;

  const { signal, cancel } = timeoutSignal(timeoutMs);

  try {
    const res = await fetch(url, {
      method,
      headers: { "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal,
    });

    const text = await res.text();
    const data = text ? JSON.parse(text) : null;

    if (!res.ok) {
      return {
        ok: false,
        error: `HTTP ${res.status}: ${data?.error ?? text ?? "Request failed"}`,
      };
    }

    return data as T;
  } catch (e: any) {
    const msg =
      e?.name === "AbortError"
        ? "Request timed out (ESP32 not reachable)"
        : (e?.message ?? "Network error");
    return { ok: false, error: msg };
  } finally {
    cancel();
  }
}
