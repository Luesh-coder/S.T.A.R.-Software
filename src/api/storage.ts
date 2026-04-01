import AsyncStorage from "@react-native-async-storage/async-storage";
import { DEFAULT_ESP32_HOST } from "./config";

const KEY_HOST = "star.esp32.host";

/** Load the last-used ESP32 host, falling back to the default AP IP. */
export async function getSavedHost(): Promise<string> {
  const saved = await AsyncStorage.getItem(KEY_HOST);
  return saved ?? DEFAULT_ESP32_HOST;
}

/** Persist the ESP32 host so it survives app restarts. */
export async function saveHost(host: string): Promise<void> {
  await AsyncStorage.setItem(KEY_HOST, host);
}
