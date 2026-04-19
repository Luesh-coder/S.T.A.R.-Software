import { Ionicons } from "@expo/vector-icons";
import { useFocusEffect } from "@react-navigation/native";
import { useRouter } from "expo-router";
import React, { useCallback, useEffect, useState } from "react";
import {
  Alert,
  SafeAreaView,
  Text,
  useWindowDimensions,
  View,
} from "react-native";
import { useEsp32 } from "../src/api/useEsp32";
import { AnimatedButton } from "../src/components/AnimatedButton";
import { StatusInfo } from "../src/components/StatusInfo";

export default function ManualScreen() {
  const router = useRouter();
  const { height } = useWindowDimensions();
  const {
    status,
    connection,
    refresh,
    wsConnect,
    wsDisconnect,
    wsMove,
    wsStop,
    apiSetMode,
    apiSetTracking,
  } = useEsp32();

  const [controlActive, setControlActive] = useState(false);
  const [trackingWasActive, setTrackingWasActive] = useState(false);

  useEffect(() => {
    wsConnect();
    return () => wsDisconnect();
  }, [wsConnect, wsDisconnect]);

  useFocusEffect(
    useCallback(() => {
      if (connection === "online" && status?.mode !== "manual") {
        apiSetMode("manual");
      }
    }, [connection, status?.mode, apiSetMode]),
  );

  const compact = height < 860;
  const dPadIcon = compact ? 34 : 40;
  const dPadGap = compact ? 10 : 14;
  const hold = (dir: "up" | "down" | "left" | "right") => () => wsMove(dir);

  const handleControlToggle = async () => {
    if (!controlActive && connection !== "online") {
      Alert.alert("ESP32", "Not connected — please connect to STAR-ESP32 Wi-Fi");
      return;
    }
    if (!controlActive) {
      const wasTracking = status?.tracking ?? false;
      setTrackingWasActive(wasTracking);
      if (wasTracking) {
        await apiSetTracking(false);
      }
    } else {
      wsStop();
      if (trackingWasActive) {
        await apiSetTracking(true);
      }
    }
    setControlActive((prev) => !prev);
  };

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center px-6 justify-evenly">
        <View className="items-center">
          <Text className="text-star-text text-[54px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
          <StatusInfo connection={connection} status={status} />
        </View>

        <View className="w-full max-w-[520px] gap-4">
          {/* Start / Stop Control */}
          <AnimatedButton
            onPress={handleControlToggle}
            className="h-12 w-full items-center justify-center rounded-pill bg-star-button"
          >
            <Text className="text-[17px] font-normal text-star-buttonText">
              {controlActive ? "Stop Control" : "Start Control"}
            </Text>
          </AnimatedButton>

          {/* Auto */}
          <AnimatedButton
            onPress={async () => {
              wsStop();
              if (controlActive && trackingWasActive) {
                await apiSetTracking(true);
              }
              setControlActive(false);
              router.push("/");
            }}
            className="h-12 w-full items-center justify-center rounded-pill bg-star-button"
          >
            <Text className="text-[17px] font-normal text-star-buttonText">
              Auto
            </Text>
          </AnimatedButton>
        </View>

        {/* D-Pad */}
        <View
          className="items-center justify-center"
          style={{ opacity: controlActive ? 1 : 0.3 }}
        >
          <AnimatedButton
            onPressIn={hold("up")}
            onPressOut={wsStop}
            disabled={!controlActive}
            className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginBottom: dPadGap }}
          >
            <Ionicons name="chevron-up" size={dPadIcon} color="#2F344B" />
          </AnimatedButton>

          <View
            className="flex-row items-center justify-center"
            style={{ gap: dPadGap * 3 }}
          >
            <AnimatedButton
              onPressIn={hold("left")}
              onPressOut={wsStop}
              disabled={!controlActive}
              className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
            >
              <Ionicons name="chevron-back" size={dPadIcon} color="#2F344B" />
            </AnimatedButton>

            <AnimatedButton
              onPressIn={hold("right")}
              onPressOut={wsStop}
              disabled={!controlActive}
              className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
            >
              <Ionicons name="chevron-forward" size={dPadIcon} color="#2F344B" />
            </AnimatedButton>
          </View>

          <AnimatedButton
            onPressIn={hold("down")}
            onPressOut={wsStop}
            disabled={!controlActive}
            className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginTop: dPadGap }}
          >
            <Ionicons name="chevron-down" size={dPadIcon} color="#2F344B" />
          </AnimatedButton>
        </View>

        {/* Refresh */}
        <AnimatedButton onPress={refresh}>
          <Text className="text-star-text text-sm opacity-60">
            Tap to refresh status
          </Text>
        </AnimatedButton>
      </View>
    </SafeAreaView>
  );
}
