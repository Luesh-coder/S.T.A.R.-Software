import { apiLog } from "./config";

// ─── Types ────────────────────────────────────────────────────────────────────

export type WsState = "idle" | "connecting" | "open" | "closed" | "error";

export type ManualCommand =
  | { type: "move"; dir: "up" | "down" | "left" | "right" }
  | { type: "stop" };

// ─── WebSocket Client ─────────────────────────────────────────────────────────

/**
 * Manages the persistent WebSocket connection used for manual gimbal control.
 * Connect once, then call send() for each D-pad input.
 */
export class ManualWsClient {
  private ws: WebSocket | null = null;

  state: WsState = "idle";
  onState?: (s: WsState) => void;
  onMessage?: (msg: unknown) => void;

  constructor(private url: string) {}

  connect() {
    if (this.ws) return; // already connected or connecting
    apiLog("WS connecting →", this.url);
    this.setState("connecting");

    const ws = new WebSocket(this.url);
    this.ws = ws;

    ws.onopen = () => {
      apiLog("WS open ✓", this.url);
      this.setState("open");
    };

    ws.onclose = (ev) => {
      apiLog(`WS closed (code=${ev.code})`, this.url);
      this.teardown();
      this.setState("closed");
    };

    ws.onerror = () => {
      apiLog("WS error", this.url);
      this.setState("error");
      // onclose fires after onerror — teardown happens there
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
    if (!this.ws) return;
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

  private teardown() {
    if (!this.ws) return;
    this.ws.onopen = null;
    this.ws.onclose = null;
    this.ws.onerror = null;
    this.ws.onmessage = null;
    this.ws = null;
  }
}
