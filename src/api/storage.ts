import AsyncStorage from "@react-native-async-storage/async-storage";
import { DEFAULT_ESP32_HOST } from "./config";

const KEY_HOST = "star.esp32.host";

export async function getSavedHost(): Promise<string> {
  const v = await AsyncStorage.getItem(KEY_HOST);
  return v ?? DEFAULT_ESP32_HOST;
}

export async function saveHost(host: string) {
  await AsyncStorage.setItem(KEY_HOST, host);
}
