import { useCallback, useEffect, useMemo, useState } from "react";
import { makeWsUrl } from "./config";
import { getStatus, newTarget, setLight, setMode, setTracking } from "./esp32";
import { getSavedHost, saveHost } from "./storage";
import type { StarMode, StatusResponse } from "./types";
import { ManualWsClient, WsState } from "./ws";

export function useEsp32() {
  const [host, setHost] = useState<string>("192.168.4.1");
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [wsState, setWsState] = useState<WsState>("idle");

  // WS client (manual control)
  const wsUrl = useMemo(() => makeWsUrl(host, "/ws/manual"), [host]);
  const wsClient = useMemo(() => new ManualWsClient(wsUrl), [wsUrl]);

  useEffect(() => {
    wsClient.onState = setWsState;
    return () => wsClient.disconnect();
  }, [wsClient]);

  useEffect(() => {
    (async () => {
      const h = await getSavedHost();
      setHost(h);
    })();
  }, []);

  const refresh = useCallback(async () => {
    setError(null);
    const res = await getStatus(host);
    if ("ok" in res && res.ok === false) {
      setError(res.error);
      return;
    }
    setStatus(res);
  }, [host]);

  const setHostAndSave = useCallback(async (h: string) => {
    setHost(h);
    await saveHost(h);
  }, []);

  const apiSetMode = useCallback(
    async (mode: StarMode) => {
      setError(null);
      const res = await setMode(host, mode);
      if ("ok" in res && res.ok === false) return setError(res.error);
      await refresh();
    },
    [host, refresh],
  );

  const apiSetTracking = useCallback(
    async (enabled: boolean) => {
      setError(null);
      const res = await setTracking(host, enabled);
      if ("ok" in res && res.ok === false) return setError(res.error);
      await refresh();
    },
    [host, refresh],
  );

  const apiNewTarget = useCallback(async () => {
    setError(null);
    const res = await newTarget(host);
    if ("ok" in res && res.ok === false) return setError(res.error);
  }, [host]);

  const apiToggleLight = useCallback(async () => {
    setError(null);
    const enabled = !(status?.light ?? false);
    const res = await setLight(host, enabled);
    if ("ok" in res && res.ok === false) return setError(res.error);
    await refresh();
  }, [host, refresh, status?.light]);

  // Manual WS
  const wsConnect = useCallback(() => wsClient.connect(), [wsClient]);
  const wsDisconnect = useCallback(() => wsClient.disconnect(), [wsClient]);
  const wsMove = useCallback(
    (dir: "up" | "down" | "left" | "right") =>
      wsClient.send({ type: "move", dir }),
    [wsClient],
  );
  const wsStop = useCallback(() => wsClient.send({ type: "stop" }), [wsClient]);

  return {
    host,
    setHostAndSave,
    status,
    error,
    refresh,

    apiSetMode,
    apiSetTracking,
    apiNewTarget,
    apiToggleLight,

    wsState,
    wsConnect,
    wsDisconnect,
    wsMove,
    wsStop,
  };
}
