import { useFocusEffect } from "@react-navigation/native";
import { useRouter } from "expo-router";
import React, { useCallback, useEffect } from "react";
import {
  SafeAreaView,
  Text,
  Alert,
  View,
} from "react-native";
import { useEsp32 } from "../src/api/useEsp32";
import { AnimatedButton } from "../src/components/AnimatedButton";
import { StatusInfo } from "../src/components/StatusInfo";

type Action = { label: string; onPress: () => Promise<void> | void; disabled?: boolean; dimmed?: boolean };

export default function Index() {
  const router = useRouter();

  const {
    status,
    connection,
    refresh,
    apiSetTracking,
    apiNewTarget,
    apiSetMode,
  } = useEsp32();

  const requireConnection = (fn: () => Promise<void> | void) => async () => {
    if (connection !== "online") {
      Alert.alert("ESP32", "Not connected — please connect to STAR-ESP32 Wi-Fi");
      return;
    }
    await fn();
  };

  useEffect(() => {
    refresh();
  }, [refresh]);

  useFocusEffect(
    useCallback(() => {
      if (connection === "online" && status?.mode !== "auto") {
        apiSetMode("auto");
      }
    }, [connection, status?.mode, apiSetMode]),
  );

  const tracking = status?.tracking ?? false;
  const trackingLabel = tracking ? "Stop Tracking" : "Start Tracking";

  const actions: Action[] = [
    {
      label: trackingLabel,
      onPress: requireConnection(async () => apiSetTracking(!tracking)),
    },
    {
      label: "New Target",
      onPress: requireConnection(async () => apiNewTarget()),
      disabled: !tracking,
      dimmed: !tracking,
    },
    {
      label: "Manual",
      onPress: () => router.push("/manual"),
    },
  ];

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center justify-evenly px-6">
        <View className="items-center">
          <Text className="text-star-text text-[54px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
          <StatusInfo connection={connection} status={status} />
        </View>

        <View className="w-full max-w-[420px] gap-5">
          {actions.map((a) => (
            <AnimatedButton
              key={a.label}
              onPress={a.onPress}
              disabled={a.disabled}
              className="h-14 w-full items-center justify-center rounded-pill bg-star-button"
              style={{ opacity: a.dimmed ? 0.35 : 1 }}
            >
              <Text className="text-[18px] font-normal text-star-buttonText">
                {a.label}
              </Text>
            </AnimatedButton>
          ))}
        </View>

        <AnimatedButton onPress={refresh}>
          <Text className="text-star-text text-sm opacity-60">
            Tap to refresh status
          </Text>
        </AnimatedButton>
      </View>
    </SafeAreaView>
  );
}
