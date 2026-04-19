import { useCallback, useEffect, useMemo, useState } from "react";
import { makeWsUrl, POLL_INTERVAL_MS } from "./config";
import { getStatus, newTarget, setLight, setMode, setTracking } from "./esp32";
import { getSavedHost, saveHost } from "./storage";
import { ConnectionState, isApiErr, StarMode, StatusResponse } from "./types";
import { ManualWsClient, WsState } from "./ws";

// ─── Hook ─────────────────────────────────────────────────────────────────────

/**
 * Central hook for all ESP32 communication.
 *
 * Handles:
 *  - Host persistence across app restarts
 *  - HTTP REST commands (mode, tracking, light, new-target)
 *  - Periodic status polling while connected
 *  - WebSocket session for manual D-pad control
 */
export function useEsp32() {
  const [host, setHost] = useState<string>("192.168.4.1");
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [connection, setConnection] = useState<ConnectionState>("unknown");
  const [wsState, setWsState] = useState<WsState>("idle");

  // ── WebSocket client ────────────────────────────────────────────────────────

  const wsUrl = useMemo(() => makeWsUrl(host), [host]);
  const wsClient = useMemo(() => new ManualWsClient(wsUrl), [wsUrl]);

  useEffect(() => {
    wsClient.onState = setWsState;
    return () => wsClient.disconnect();
  }, [wsClient]);

  // ── Load saved host on mount ────────────────────────────────────────────────

  useEffect(() => {
    getSavedHost().then(setHost);
  }, []);

  // ── Status polling ──────────────────────────────────────────────────────────

  const refresh = useCallback(async () => {
    const res = await getStatus(host);
    if (isApiErr(res)) {
      setConnection("offline");
      return;
    }
    setError(null);
    setStatus(res);
    setConnection("online");
  }, [host]);

  // Self-scheduling poll: next tick is queued only after the previous one
  // resolves, so slow/timed-out polls can't overlap. Cleans up on host change
  // or unmount without leaving stray intervals.
  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const tick = async () => {
      if (cancelled) return;
      const res = await getStatus(host);
      if (cancelled) return;
      if (isApiErr(res)) {
        setConnection("offline");
      } else {
        setError(null);
        setStatus(res);
        setConnection("online");
      }
      timer = setTimeout(tick, POLL_INTERVAL_MS);
    };

    tick();
    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
  }, [host]);

  // ── Host management ─────────────────────────────────────────────────────────

  const setHostAndSave = useCallback(async (h: string) => {
    setHost(h);
    setConnection("unknown");
    await saveHost(h);
  }, []);

  // ── Gimbal Commands ─────────────────────────────────────────────────────────

  const apiSetMode = useCallback(
    async (mode: StarMode) => {
      setError(null);
      const res = await setMode(host, mode);
      if (isApiErr(res)) { setError(res.error); return; }
      setStatus(prev => prev ? { ...prev, mode } : prev);
    },
    [host],
  );

  const apiSetTracking = useCallback(
    async (enabled: boolean) => {
      setError(null);
      const res = await setTracking(host, enabled);
      if (isApiErr(res)) { setError(res.error); return; }
      await refresh();
    },
    [host, refresh],
  );

  const apiNewTarget = useCallback(async () => {
    setError(null);
    const res = await newTarget(host);
    if (isApiErr(res)) { setError(res.error); return; }
    await refresh();
  }, [host, refresh]);

  const apiToggleLight = useCallback(async () => {
    setError(null);
    const enabled = !(status?.light ?? false);
    const res = await setLight(host, enabled);
    if (isApiErr(res)) { setError(res.error); return; }
    await refresh();
  }, [host, refresh, status?.light]);

  // ── Manual WebSocket controls ───────────────────────────────────────────────

  const wsConnect    = useCallback(() => wsClient.connect(), [wsClient]);
  const wsDisconnect = useCallback(() => wsClient.disconnect(), [wsClient]);
  const wsMove       = useCallback(
    (dir: "up" | "down" | "left" | "right") => wsClient.send({ type: "move", dir }),
    [wsClient],
  );
  const wsStop = useCallback(() => wsClient.send({ type: "stop" }), [wsClient]);

  // ── Public API ──────────────────────────────────────────────────────────────

  return {
    // Connection
    host,
    setHostAndSave,
    connection,
    error,
    refresh,

    // Status
    status,

    // Gimbal commands
    apiSetMode,
    apiSetTracking,
    apiNewTarget,
    apiToggleLight,

    // Manual mode (WebSocket)
    wsState,
    wsConnect,
    wsDisconnect,
    wsMove,
    wsStop,
  };
}
