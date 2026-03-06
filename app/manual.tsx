import { Ionicons } from "@expo/vector-icons";
import { useRouter } from "expo-router";
import React, { useEffect, useMemo } from "react";
import { Alert, Pressable, SafeAreaView, Text, View } from "react-native";
import { useEsp32 } from "../src/api/useEsp32";

type TopAction = { label: string; onPress: () => Promise<void> | void };

export default function ManualScreen() {
  const router = useRouter();
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

  const dotClass = useMemo(() => {
    if (wsState === "open") return "bg-green-400";
    if (wsState === "connecting") return "bg-yellow-400";
    return "bg-red-400";
  }, [wsState]);

  const actions: TopAction[] = [
    { label: "Start / Stop", onPress: async () => {} }, // optional: map to REST tracking if you want
    { label: "Toggle Light", onPress: async () => {} }, // optional: map to REST
    {
      label: "Auto",
      onPress: async () => {
        wsStop();
        await apiSetMode("auto");
        router.push("./");
      },
    },
  ];

  const hold = (dir: "up" | "down" | "left" | "right") => () => wsMove(dir);

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center px-6 justify-between">
        <View className="w-full items-center">
          <View className="pt-14">
            <Text className="text-star-text text-[72px] font-light tracking-[0.28em]">
              S.T.A.R.
            </Text>
          </View>

          <View className="flex-row items-center gap-2 mt-2">
            <View className={`h-2.5 w-2.5 rounded-full ${dotClass}`} />
            <Text className="text-star-text text-sm opacity-60">
              WS: {wsState}
            </Text>
          </View>

          <View className="mt-12 w-full max-w-[520px] gap-7">
            {actions.map((a) => (
              <Pressable
                key={a.label}
                onPress={a.onPress}
                className="h-20 w-full items-center justify-center rounded-pill bg-star-button"
              >
                <Text className="text-[22px] font-normal text-star-buttonText">
                  {a.label}
                </Text>
              </Pressable>
            ))}
          </View>

          <View className="h-14" />
        </View>

        {/* D-Pad */}
        <View className="pb-16 items-center justify-center">
          <Pressable
            onPressIn={hold("up")}
            onPressOut={wsStop}
            className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginBottom: 14 }}
          >
            <Ionicons name="chevron-up" size={40} color="#2F344B" />
          </Pressable>

          <View className="flex-row items-center justify-center">
            <Pressable
              onPressIn={hold("left")}
              onPressOut={wsStop}
              className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
              style={{ marginRight: 14 }}
            >
              <Ionicons name="chevron-back" size={40} color="#2F344B" />
            </Pressable>

            <Pressable
              onPress={wsStop}
              className="h-[86px] w-[86px] items-center justify-center rounded-2xl bg-star-button opacity-50"
            >
              <Ionicons name="stop" size={32} color="#2F344B" />
            </Pressable>

            <Pressable
              onPressIn={hold("right")}
              onPressOut={wsStop}
              className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
              style={{ marginLeft: 14 }}
            >
              <Ionicons name="chevron-forward" size={40} color="#2F344B" />
            </Pressable>
          </View>

          <Pressable
            onPressIn={hold("down")}
            onPressOut={wsStop}
            className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginTop: 14 }}
          >
            <Ionicons name="chevron-down" size={40} color="#2F344B" />
          </Pressable>
        </View>
      </View>
    </SafeAreaView>
  );
}
