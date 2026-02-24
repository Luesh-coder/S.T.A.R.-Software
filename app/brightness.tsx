import Slider from "@react-native-community/slider";
import { useRouter } from "expo-router";
import React, { useMemo, useState } from "react";
import { Pressable, SafeAreaView, Text, View } from "react-native";

export default function BrightnessScreen() {
  const router = useRouter();

  const [brightness, setBrightness] = useState<number>(50);

  const label = useMemo(
    () => `Brightness Level: ${Math.round(brightness)}`,
    [brightness],
  );

  // TODO: Replace with your real send-to-device call (REST/WebSocket/BLE/etc.)
  function sendBrightness(value: number) {
    // Example:
    // ws.send(JSON.stringify({ type: "brightness", value: Math.round(value) }));
    console.log("Brightness ->", Math.round(value));
  }

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center px-6">
        {/* Title */}
        <View className="pt-14">
          <Text className="text-star-text text-[72px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
        </View>

        {/* Back Button */}
        <View className="mt-10 w-full max-w-[520px]">
          <Pressable
            onPress={() => router.back()}
            className="h-20 w-full items-center justify-center rounded-pill bg-star-button"
          >
            <Text className="text-[22px] font-normal text-star-buttonText">
              Back
            </Text>
          </Pressable>
        </View>

        {/* Label */}
        <Text className="mt-16 text-[34px] font-light text-star-text">
          {label}
        </Text>

        {/* Slider */}
        <View className="mt-16 w-full max-w-[620px]">
          <Slider
            value={brightness}
            onValueChange={(v) => setBrightness(v)}
            onSlidingComplete={(v) => {
              setBrightness(v);
              sendBrightness(v);
            }}
            minimumValue={0}
            maximumValue={100}
            step={1}
            minimumTrackTintColor="#ECEDEF"
            maximumTrackTintColor="#ECEDEF"
            thumbTintColor="#ECEDEF"
          />
        </View>
      </View>
    </SafeAreaView>
  );
}
