import { Ionicons } from "@expo/vector-icons";
import { useRouter } from "expo-router";
import React from "react";
import { Pressable, SafeAreaView, Text, View } from "react-native";

type TopAction = {
  label: string;
  onPress?: () => void;
};

export default function ManualScreen() {
  const router = useRouter();

  const actions: TopAction[] = [
    { label: "Start / Stop", onPress: () => console.log("Start/Stop") },
    { label: "Toggle Light", onPress: () => console.log("Toggle Light") },
    { label: "Brightness", onPress: () => router.push("./brightness") },
    { label: "Auto", onPress: () => router.push("./") },
  ];

  const sendMove = (dir: "up" | "down" | "left" | "right") => {
    console.log("MOVE:", dir);
  };

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      {/* ✅ justify-between makes top section stay up and D-pad stay down with consistent spacing */}
      <View className="flex-1 items-center px-6 justify-between">
        {/* Top section (Title + Buttons) */}
        <View className="w-full items-center">
          <View className="pt-14">
            <Text className="text-star-text text-[72px] font-light tracking-[0.28em]">
              S.T.A.R.
            </Text>
          </View>

          {/* Buttons */}
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

          {/* ✅ Adds EXTRA separation between buttons and D-pad */}
          <View className="h-14" />
        </View>

        {/* D-Pad (Bottom section) */}
        <View className="pb-16 items-center justify-center">
          {/* Up */}
          <Pressable
            onPressIn={() => sendMove("up")}
            className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
            style={{ marginBottom: 14 }}
          >
            <Ionicons name="chevron-up" size={40} color="#2F344B" />
          </Pressable>

          <View className="flex-row items-center justify-center">
            {/* Left */}
            <Pressable
              onPressIn={() => sendMove("left")}
              className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
              style={{ marginRight: 14 }}
            >
              <Ionicons name="chevron-back" size={40} color="#2F344B" />
            </Pressable>

            {/* Center */}
            <View className="h-[86px] w-[86px] rounded-2xl bg-transparent" />

            {/* Right */}
            <Pressable
              onPressIn={() => sendMove("right")}
              className="h-[86px] w-[86px] items-center justify-center rounded-pill bg-star-button"
              style={{ marginLeft: 14 }}
            >
              <Ionicons name="chevron-forward" size={40} color="#2F344B" />
            </Pressable>
          </View>

          {/* Down */}
          <Pressable
            onPressIn={() => sendMove("down")}
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
