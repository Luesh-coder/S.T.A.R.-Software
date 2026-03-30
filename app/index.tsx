import { useRouter } from "expo-router";
import React, { useEffect } from "react";
import {
  Alert,
  Pressable,
  SafeAreaView,
  Text,
  useWindowDimensions,
  View,
} from "react-native";
import { useEsp32 } from "../src/api/useEsp32";

type Action = { label: string; onPress: () => Promise<void> | void };

export default function Index() {
  const router = useRouter();
  const { height } = useWindowDimensions();
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

  const compact = height < 760;

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
        await apiSetMode("manual");
        router.push("/manual");
      },
    },
  ];

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
<<<<<<< HEAD
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
=======
      <View className="flex-1 items-center px-5" style={{ paddingTop: compact ? 20 : 44 }}>
        <Text
          className="text-star-text font-light tracking-[0.28em]"
          style={{ fontSize: compact ? 52 : 72 }}
        >
          S.T.A.R.
        </Text>

        <View className="items-center" style={{ marginTop: compact ? 4 : 8 }}>
          <Text className="text-star-text opacity-70">
            Mode: {status?.mode ?? "—"} • Tracking: {status?.tracking ? "ON" : "OFF"}
          </Text>
        </View>

        <View className="w-full max-w-[420px]" style={{ marginTop: compact ? 28 : 64, gap: compact ? 12 : 24 }}>
>>>>>>> 03d95a2e29250535dd4bf184c9ec6375f878266e
          {actions.map((a) => (
            <Pressable
              key={a.label}
              onPress={a.onPress}
<<<<<<< HEAD
              className="h-14 w-full items-center justify-center rounded-pill bg-star-button"
            >
              <Text className="text-[18px] font-normal text-star-buttonText">
=======
              className="w-full items-center justify-center rounded-pill bg-star-button"
              style={{ height: compact ? 58 : 80 }}
            >
              <Text className="font-normal text-star-buttonText" style={{ fontSize: compact ? 18 : 22 }}>
>>>>>>> 03d95a2e29250535dd4bf184c9ec6375f878266e
                {a.label}
              </Text>
            </Pressable>
          ))}
        </View>

<<<<<<< HEAD
        <Pressable onPress={refresh}>
          <Text className="text-star-text text-xs opacity-60">
            Tap to refresh status
          </Text>
=======
        <Pressable onPress={refresh} style={{ marginTop: compact ? 20 : 40 }}>
          <Text className="text-star-text opacity-60">Tap to refresh status</Text>
>>>>>>> 03d95a2e29250535dd4bf184c9ec6375f878266e
        </Pressable>
      </View>
    </SafeAreaView>
  );
}
