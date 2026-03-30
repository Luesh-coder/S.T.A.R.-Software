import { useRouter } from "expo-router";
import React, { useEffect } from "react";
import { Alert, Pressable, SafeAreaView, Text, View } from "react-native";
import { useEsp32 } from "../src/api/useEsp32";

type Action = { label: string; onPress: () => Promise<void> | void };

export default function Index() {
  const router = useRouter();
  const {
    status,
    error,
    refresh,
    apiSetTracking,
    apiNewTarget,
    apiToggleLight,
    apiSetMode,
  } = useEsp32();

  useEffect(() => {
    refresh();
  }, [refresh]);

  useEffect(() => {
    if (error) Alert.alert("ESP32", error);
  }, [error]);

  const trackingLabel = status?.tracking ? "Stop Tracking" : "Start Tracking";

  const actions: Action[] = [
    {
      label: trackingLabel,
      onPress: async () => apiSetTracking(!(status?.tracking ?? false)),
    },
    {
      label: "New Target",
      onPress: async () => apiNewTarget(),
    },
    {
      label: "Toggle Light",
      onPress: async () => apiToggleLight(),
    },
    {
      label: "Manual",
      onPress: async () => {
        // Put ESP32 in manual mode first
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
          {actions.map((a) => (
            <Pressable
              key={a.label}
              onPress={a.onPress}
              className="h-14 w-full items-center justify-center rounded-pill bg-star-button"
            >
              <Text className="text-[18px] font-normal text-star-buttonText">
                {a.label}
              </Text>
            </Pressable>
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
