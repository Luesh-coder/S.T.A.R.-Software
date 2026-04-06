import { useRouter } from "expo-router";
import React, { useEffect, useState } from "react";
import {
  Pressable,
  SafeAreaView,
  Text,
  Alert,
  View,
} from "react-native";
import { useEsp32 } from "../src/api/useEsp32";
import { AnimatedButton } from "../src/components/AnimatedButton";

type Action = { label: string; onPress: () => Promise<void> | void };

export default function Index() {
  const router = useRouter();

  const {
    status,
    connection,
    error,
    refresh,
    apiSetTracking,
    apiNewTarget,
    apiToggleLight,
    apiSetMode,
  } = useEsp32();

  const [connError, setConnError] = useState<string | null>(null);

  const requireConnection = (fn: () => Promise<void> | void) => async () => {
    if (connection !== "online") {
      setConnError("App not connected, please connect to STAR-ESP32 in your Wifi Settings");
      return;
    }
    setConnError(null);
    await fn();
  };

  useEffect(() => {
    refresh();
  }, [refresh]);

  useEffect(() => {
    if (error) Alert.alert("ESP32", error);
  }, [error]);

  // Auto-dismiss the connection error when connection comes back online
  useEffect(() => {
    if (connection === "online") setConnError(null);
  }, [connection]);

  const trackingLabel = status?.tracking ? "Stop Tracking" : "Start Tracking";

  const actions: Action[] = [
    {
      label: trackingLabel,
      onPress: requireConnection(async () => apiSetTracking(!(status?.tracking ?? false))),
    },
    {
      label: "New Target",
      onPress: requireConnection(async () => apiNewTarget()),
    },
    {
      label: "Toggle Light",
      onPress: requireConnection(async () => apiToggleLight()),
    },
    {
      label: "Manual",
      onPress: async () => {
        await apiSetMode("manual");
        router.push("/manual");
      },
    },
  ];

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center justify-evenly px-6">
        <View className="items-center">
          <Text className="text-star-text text-[42px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
          <Text className="text-star-text text-xs opacity-70 mt-2">
            Mode: {status?.mode ?? "—"} • Tracking:{" "}
            {status?.tracking ? "ON" : "OFF"}
          </Text>
        </View>

        <View className="w-full max-w-[420px] gap-5">
          {connError && (
            <View className="w-full rounded-xl bg-red-500/20 border border-red-500/40 px-4 py-3">
              <Text className="text-red-400 text-sm text-center">{connError}</Text>
            </View>
          )}
          {actions.map((a) => (
            <AnimatedButton
              key={a.label}
              onPress={a.onPress}
              className="h-14 w-full items-center justify-center rounded-pill bg-star-button"
            >
              <Text className="text-[18px] font-normal text-star-buttonText">
                {a.label}
              </Text>
            </AnimatedButton>
          ))}
        </View>

        <Pressable onPress={refresh}>
          <Text className="text-star-text text-xs opacity-60">
            Tap to refresh status
          </Text>
        </Pressable>
      </View>
    </SafeAreaView>
  );
}
