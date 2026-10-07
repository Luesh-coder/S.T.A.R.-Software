import Slider from "@react-native-community/slider";
import { useFocusEffect } from "@react-navigation/native";
import { useRouter } from "expo-router";
import React, { useCallback, useEffect, useState } from "react";
import { SafeAreaView, Text, View } from "react-native";
import { useEsp32 } from "../src/api/useEsp32";
import { AnimatedButton } from "../src/components/AnimatedButton";
import { StatusInfo } from "../src/components/StatusInfo";

const PAN_RANGE  = 15;
const TILT_RANGE = 10;
const STEP       = 0.5;

const formatDeg = (v: number) =>
  `${v > 0 ? "+" : v < 0 ? "" : " "}${v.toFixed(1)}°`;

export default function CalibrateScreen() {
  const router = useRouter();
  const {
    status,
    connection,
    refresh,
    apiSetMode,
    apiSetCalibration,
  } = useEsp32();

  const [panOffset,  setPanOffset]  = useState(0);
  const [tiltOffset, setTiltOffset] = useState(0);

  useFocusEffect(
    useCallback(() => {
      if (connection === "online" && status?.mode !== "auto") {
        apiSetMode("auto");
      }
    }, [connection, status?.mode, apiSetMode]),
  );

  useEffect(() => {
    if (!status) return;
    if (status.panOffset  !== undefined) setPanOffset(status.panOffset);
    if (status.tiltOffset !== undefined) setTiltOffset(status.tiltOffset);
  }, [status?.panOffset, status?.tiltOffset]);

  const rows: {
    label: string;
    value: number;
    range: number;
    set: (v: number) => void;
    onCommit: (v: number) => void;
  }[] = [
    {
      label: "Pan",
      value: panOffset,
      range: PAN_RANGE,
      set: setPanOffset,
      onCommit: (v) => {
        setPanOffset(v);
        apiSetCalibration({ panOffset: v, tiltOffset });
      },
    },
    {
      label: "Tilt",
      value: tiltOffset,
      range: TILT_RANGE,
      set: setTiltOffset,
      onCommit: (v) => {
        setTiltOffset(v);
        apiSetCalibration({ panOffset, tiltOffset: v });
      },
    },
  ];

  return (
    <SafeAreaView className="flex-1 bg-star-bg">
      <View className="flex-1 items-center px-6 justify-evenly">
        <View className="items-center">
          <Text className="text-star-text text-[54px] font-light tracking-[0.28em]">
            S.T.A.R.
          </Text>
          <StatusInfo connection={connection} status={status} />
        </View>

        <View className="w-full max-w-[520px] gap-8">
          {rows.map((r) => (
            <View key={r.label} className="w-full">
              <View className="flex-row justify-between mb-1">
                <Text className="text-star-text text-[16px]">{r.label}</Text>
                <Text className="text-star-muted text-[16px]">{formatDeg(r.value)}</Text>
              </View>
              <Slider
                minimumValue={-r.range}
                maximumValue={+r.range}
                step={STEP}
                value={r.value}
                onValueChange={r.set}
                onSlidingComplete={r.onCommit}
                minimumTrackTintColor="rgba(236,237,239,0.25)"
                maximumTrackTintColor="rgba(236,237,239,0.25)"
                thumbTintColor="#ECEDEF"
              />
            </View>
          ))}
        </View>

        <View className="w-full max-w-[420px] gap-4">
          <AnimatedButton
            onPress={() => router.back()}
            className="h-14 w-full items-center justify-center rounded-pill bg-star-button"
          >
            <Text className="text-[18px] font-normal text-star-buttonText">
              Back
            </Text>
          </AnimatedButton>

          <AnimatedButton onPress={refresh}>
            <Text className="text-star-text text-sm opacity-60 text-center">
              Tap to refresh status
            </Text>
          </AnimatedButton>
        </View>
      </View>
    </SafeAreaView>
  );
}
