import React from "react";
import { View, Text } from "react-native";
import { ConnectionState, StatusResponse } from "../api/types";

type Props = {
  connection: ConnectionState;
  status: StatusResponse | null;
};

export function StatusInfo({ connection, status }: Props) {
  const isConnected = connection === "online";

  return (
    <View className="items-center gap-1.5 mt-2">
      <View className="flex-row items-center gap-2">
        <View
          className={`h-3 w-3 rounded-full ${
            isConnected ? "bg-star-success" : "bg-star-danger"
          }`}
        />
        <Text className="text-star-text text-sm opacity-70">
          {isConnected ? "Connected" : "No Connection"}
        </Text>
      </View>

      <View className="flex-row items-center gap-3">
        <Text className="text-star-text text-sm opacity-70">
          Mode: {status?.mode === "auto" ? "Auto" : status?.mode === "manual" ? "Manual" : "—"}
        </Text>
        <Text className="text-star-text text-sm opacity-40">•</Text>
        <Text className="text-star-text text-sm opacity-70">
          Tracking: {status?.tracking ? "ON" : "OFF"}
        </Text>
      </View>
    </View>
  );
}
