export type WsState = "idle" | "connecting" | "open" | "closed" | "error";

export type ManualCommand =
  | { type: "move"; dir: "up" | "down" | "left" | "right" }
  | { type: "stop" };

export class ManualWsClient {
  private ws: WebSocket | null = null;

  state: WsState = "idle";
  onState?: (s: WsState) => void;
  onMessage?: (msg: any) => void;

  constructor(private url: string) {}

  connect() {
    if (this.ws) return;
    this.setState("connecting");
    const ws = new WebSocket(this.url);
    this.ws = ws;

    ws.onopen = () => this.setState("open");
    ws.onclose = () => {
      this.cleanup();
      this.setState("closed");
    };
    ws.onerror = () => {
      // onclose may fire after this
      this.setState("error");
    };
    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data as any);
        this.onMessage?.(data);
      } catch {
        this.onMessage?.(ev.data);
      }
    };
  }

  disconnect() {
    if (!this.ws) return;
    this.ws.close();
    this.cleanup();
    this.setState("closed");
  }

  send(cmd: ManualCommand) {
    if (!this.ws || this.state !== "open") return;
    this.ws.send(JSON.stringify(cmd));
  }

  private setState(s: WsState) {
    this.state = s;
    this.onState?.(s);
  }

  private cleanup() {
    if (!this.ws) return;
    this.ws.onopen = null;
    this.ws.onclose = null;
    this.ws.onerror = null;
    this.ws.onmessage = null;
    this.ws = null;
  }
}
