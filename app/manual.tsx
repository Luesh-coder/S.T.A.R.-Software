import { Ionicons } from "@expo/vector-icons";
import { useRouter } from "expo-router";
import React, { useEffect, useMemo } from "react";
import {
  Alert,
  Pressable,
  SafeAreaView,
  Text,
  useWindowDimensions,
  View,
} from "react-native";
import { useEsp32 } from "../src/api/useEsp32";

type TopAction = { label: string; onPress: () => Promise<void> | void };

export default function ManualScreen() {
  const router = useRouter();
  const { height } = useWindowDimensions();
  const {
    wsState,
    wsConnect,
    wsDisconnect,
    wsMove,
    wsStop,
    apiSetMode,
    error,
  } = useEsp32();

  useEffect(() => {
    wsConnect();
    return () => wsDisconnect();
  }, [wsConnect, wsDisconnect]);

  useEffect(() => {
    if (error) Alert.alert("ESP32", error);
  }, [error]);

  const compact = height < 860;

  const dotClass = useMemo(() => {
    if (wsState === "open") return "bg-green-400";
    if (wsState === "connecting") return "bg-yellow-400";
    return "bg-red-400";
  }, [wsState]);

  const actions: TopAction[] = [
    { label: "Start / Stop", onPress: async () => {} },
    { label: "Toggle Light", onPress: async () => {} },
    {
      label: "Auto",
      onPress: async () => {
        wsStop();
        await apiSetMode("auto");
        router.push("./");
      },
    },
  ];

  const dPadSize = compact ? 70 : 86;
  const dPadIcon = compact ? 34 : 40;
  const middleIcon = compact ? 28 : 32;
  const dPadGap = compact ? 10 : 14;
  const hold = (dir: "up" | "down" | "left" | "right") => () => wsMove(dir);

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center px-6 justify-evenly">
        <View className="items-center">
          <Text className="text-star-text text-[42px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
          <View className="flex-row items-center gap-2 mt-2">
            <View className={`h-2 w-2 rounded-full ${dotClass}`} />
            <Text className="text-star-text text-xs opacity-60">
              WS: {wsState}
            </Text>
          </View>
        </View>

        <View className="w-full max-w-[520px] gap-4">
          {actions.map((a) => (
            <Pressable
              key={a.label}
              onPress={a.onPress}
              className="h-12 w-full items-center justify-center rounded-pill bg-star-button"
            >
              <Text className="text-[17px] font-normal text-star-buttonText">
                {a.label}
              </Text>
            </Pressable>
          ))}
        </View>

        {/* D-Pad */}
        <View className="items-center justify-center">
          <Pressable
            onPressIn={hold("up")}
            onPressOut={wsStop}
            className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginBottom: 14 }}
          >
            <Ionicons name="chevron-up" size={32} color="#2F344B" />
          </Pressable>

          <View className="flex-row items-center justify-center">
            <Pressable
              onPressIn={hold("left")}
              onPressOut={wsStop}
              className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
              style={{ marginRight: 14 }}
            >
              <Ionicons name="chevron-back" size={32} color="#2F344B" />
            </Pressable>

            <Pressable
              onPress={wsStop}
              className="h-[66px] w-[66px] items-center justify-center rounded-2xl bg-star-button opacity-50"
            >
              <Ionicons name="stop" size={26} color="#2F344B" />
            </Pressable>

            <Pressable
              onPressIn={hold("right")}
              onPressOut={wsStop}
              className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
              style={{ marginLeft: 14 }}
            >
              <Ionicons name="chevron-forward" size={32} color="#2F344B" />
            </Pressable>
          </View>

          <Pressable
            onPressIn={hold("down")}
            onPressOut={wsStop}
            className="h-[66px] w-[66px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginTop: 14 }}
          >
            <Ionicons name="chevron-down" size={32} color="#2F344B" />
          </Pressable>
        </View>
      </View>
    </SafeAreaView>
  );
}
