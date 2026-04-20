import Slider from "@react-native-community/slider";
import { useFocusEffect } from "@react-navigation/native";
import { useRouter } from "expo-router";
import React, { useCallback, useEffect, useState } from "react";
import { SafeAreaView, Text, View } from "react-native";
import { useEsp32 } from "../src/api/useEsp32";
import { AnimatedButton } from "../src/components/AnimatedButton";
import { StatusInfo } from "../src/components/StatusInfo";

const MIN_GAIN = 1.0;
const MAX_GAIN = 3.0;
const STEP = 0.05;

export default function CalibrateScreen() {
  const router = useRouter();
  const {
    status,
    connection,
    refresh,
    apiSetMode,
    apiSetCalibration,
  } = useEsp32();

  const [tiltUp,    setTiltUp]    = useState(1.0);
  const [tiltDown,  setTiltDown]  = useState(1.0);
  const [panLeft,   setPanLeft]   = useState(1.0);
  const [panRight,  setPanRight]  = useState(1.0);

  useFocusEffect(
    useCallback(() => {
      if (connection === "online" && status?.mode !== "auto") {
        apiSetMode("auto");
      }
    }, [connection, status?.mode, apiSetMode]),
  );

  useEffect(() => {
    if (!status) return;
    if (status.tiltUpGain   !== undefined) setTiltUp(status.tiltUpGain);
    if (status.tiltDownGain !== undefined) setTiltDown(status.tiltDownGain);
    if (status.panLeftGain  !== undefined) setPanLeft(status.panLeftGain);
    if (status.panRightGain !== undefined) setPanRight(status.panRightGain);
  }, [
    status?.tiltUpGain,
    status?.tiltDownGain,
    status?.panLeftGain,
    status?.panRightGain,
  ]);

  const sendAll = (next: {
    tiltUp: number; tiltDown: number; panLeft: number; panRight: number;
  }) => {
    apiSetCalibration({
      tiltUpGain:   next.tiltUp,
      tiltDownGain: next.tiltDown,
      panLeftGain:  next.panLeft,
      panRightGain: next.panRight,
    });
  };

  const rows: {
    label: string;
    value: number;
    set: (v: number) => void;
    onCommit: (v: number) => void;
  }[] = [
    {
      label: "Tilt Up",
      value: tiltUp,
      set: setTiltUp,
      onCommit: (v) => { setTiltUp(v);   sendAll({ tiltUp: v, tiltDown, panLeft, panRight }); },
    },
    {
      label: "Tilt Down",
      value: tiltDown,
      set: setTiltDown,
      onCommit: (v) => { setTiltDown(v); sendAll({ tiltUp, tiltDown: v, panLeft, panRight }); },
    },
    {
      label: "Pan Left",
      value: panLeft,
      set: setPanLeft,
      onCommit: (v) => { setPanLeft(v);  sendAll({ tiltUp, tiltDown, panLeft: v, panRight }); },
    },
    {
      label: "Pan Right",
      value: panRight,
      set: setPanRight,
      onCommit: (v) => { setPanRight(v); sendAll({ tiltUp, tiltDown, panLeft, panRight: v }); },
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

        <View className="w-full max-w-[520px] gap-5">
          {rows.map((r) => (
            <View key={r.label} className="w-full">
              <View className="flex-row justify-between mb-1">
                <Text className="text-star-text text-[16px]">{r.label}</Text>
                <Text className="text-star-muted text-[16px]">{r.value.toFixed(2)}</Text>
              </View>
              <Slider
                minimumValue={MIN_GAIN}
                maximumValue={MAX_GAIN}
                step={STEP}
                value={r.value}
                onValueChange={r.set}
                onSlidingComplete={r.onCommit}
                minimumTrackTintColor="#6D7CFF"
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
