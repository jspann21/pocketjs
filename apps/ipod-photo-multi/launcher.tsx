import { For, createSignal } from "solid-js";
import { Text, View } from "@pocketjs/framework/components";
import { BTN } from "@pocketjs/framework/input";
import { onButtonPress } from "@pocketjs/framework/lifecycle";

interface LauncherBridge {
  selected: number;
}

interface IpodHostUi {
  __ipodApps?: string[];
  __ipodLauncher?: LauncherBridge;
}

export default function IpodPhotoLauncher() {
  const host = (globalThis as typeof globalThis & { ui?: IpodHostUi }).ui;
  const apps = host?.__ipodApps ?? [];
  const bridge = host?.__ipodLauncher;
  const [selected, setSelected] = createSignal(0);

  onButtonPress(BTN.LEFT | BTN.RIGHT, (pressed) => {
    if (apps.length === 0) return;
    const direction = pressed & BTN.RIGHT ? 1 : -1;
    setSelected((current) => (current + direction + apps.length) % apps.length);
  });
  onButtonPress(BTN.CIRCLE, () => {
    if (bridge && apps.length !== 0) bridge.selected = selected();
  });

  return (
    <View class="w-full h-full flex-col px-4 py-4 bg-slate-950">
      <Text class="text-lg text-white font-bold">POCKETJS LAUNCH</Text>
      <Text class="text-xs text-cyan-300">APPS: {apps.length}</Text>

      <View class="flex-1 flex-col justify-center gap-2">
        <For each={apps}>
          {(name, index) => (
            <Text class="text-sm text-white">
              {`${selected() === index() ? ">" : " "} ${name}`}
            </Text>
          )}
        </For>
      </View>

      <Text class="text-xs text-slate-400">Wheel - center to open</Text>
    </View>
  );
}
