import { useRouter } from "expo-router";
import React from "react";
import { Pressable, SafeAreaView, Text, View } from "react-native";

type Action = {
  label: string;
  onPress?: () => void;
};

export default function Index() {
  const router = useRouter(); // ✅ define router

  const actions: Action[] = [
    { label: "Start / Stop", onPress: () => console.log("Start/Stop") },
    { label: "New Target", onPress: () => console.log("New Target") },
    { label: "Toggle Light", onPress: () => console.log("Toggle Light") },
    { label: "Brightness", onPress: () => router.push("/brightness") },
    { label: "Manual", onPress: () => router.push("/manual") },
  ];

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center px-6">
        {/* Title */}
        <View className="pt-14">
          <Text className="text-star-text text-[72px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
        </View>

        {/* Buttons */}
        <View className="mt-16 w-full max-w-[420px] gap-8">
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
      </View>
    </SafeAreaView>
  );
}
