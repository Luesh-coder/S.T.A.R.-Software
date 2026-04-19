import { apiLog } from "./config";

// ─── Types ────────────────────────────────────────────────────────────────────

export type WsState = "idle" | "connecting" | "open" | "closed" | "error";

export type ManualCommand =
  | { type: "move"; dir: "up" | "down" | "left" | "right" }
  | { type: "stop" };

// ─── Tuning ───────────────────────────────────────────────────────────────────

const RECONNECT_DELAY_MS = 2000;
const PING_INTERVAL_MS   = 10000;

// ─── WebSocket Client ─────────────────────────────────────────────────────────

/**
 * Manages the persistent WebSocket connection used for manual gimbal control.
 * Connect once, then call send() for each D-pad input.
 *
 * Self-healing: if the connection drops while shouldReconnect is true,
 * reconnect is scheduled automatically. A periodic app-side ping keeps
 * the link warm through Wi-Fi/NAT idle timeouts.
 */
export class ManualWsClient {
  private ws: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private shouldReconnect = false;

  state: WsState = "idle";
  onState?: (s: WsState) => void;
  onMessage?: (msg: unknown) => void;

  constructor(private url: string) {}

  connect() {
    if (this.ws) return; // already connected or connecting
    this.shouldReconnect = true;
    this.clearReconnectTimer();
    apiLog("WS connecting →", this.url);
    this.setState("connecting");

    const ws = new WebSocket(this.url);
    this.ws = ws;

    ws.onopen = () => {
      apiLog("WS open ✓", this.url);
      this.setState("open");
      this.startPing();
    };

    ws.onclose = (ev) => {
      apiLog(`WS closed (code=${ev.code} reason="${ev.reason ?? ""}")`, this.url);
      this.teardown();
      this.setState("closed");
      if (this.shouldReconnect) {
        apiLog(`WS reconnecting in ${RECONNECT_DELAY_MS}ms`);
        this.reconnectTimer = setTimeout(() => {
          this.reconnectTimer = null;
          this.connect();
        }, RECONNECT_DELAY_MS);
      }
    };

    ws.onerror = (ev: any) => {
      apiLog("WS error", this.url, ev?.message ?? ev);
      this.setState("error");
      // onclose fires after onerror — teardown + reconnect happen there
    };

    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data as string);
        apiLog("WS ←", data);
        this.onMessage?.(data);
      } catch {
        apiLog("WS ← (raw)", ev.data);
        this.onMessage?.(ev.data);
      }
    };
  }

  disconnect() {
    this.shouldReconnect = false;
    this.clearReconnectTimer();
    if (!this.ws) {
      this.setState("closed");
      return;
    }
    apiLog("WS disconnecting", this.url);
    this.ws.close();
    this.teardown();
    this.setState("closed");
  }

  send(cmd: ManualCommand) {
    if (!this.ws || this.state !== "open") {
      apiLog("WS send skipped — not open:", cmd);
      return;
    }
    apiLog("WS →", cmd);
    this.ws.send(JSON.stringify(cmd));
  }

  // ── Private ──────────────────────────────────────────────────────────────────

  private setState(s: WsState) {
    this.state = s;
    this.onState?.(s);
  }

  private startPing() {
    this.stopPing();
    this.pingTimer = setInterval(() => {
      if (this.ws && this.state === "open") {
        this.ws.send('{"type":"ping"}');
      }
    }, PING_INTERVAL_MS);
  }

  private stopPing() {
    if (this.pingTimer) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }

  private clearReconnectTimer() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private teardown() {
    this.stopPing();
    if (!this.ws) return;
    this.ws.onopen = null;
    this.ws.onclose = null;
    this.ws.onerror = null;
    this.ws.onmessage = null;
    this.ws = null;
  }
}
