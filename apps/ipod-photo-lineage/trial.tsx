import { Text, View } from "@pocketjs/framework/components";
import { BTN } from "@pocketjs/framework/input";
import { onButtonPress } from "@pocketjs/framework/lifecycle";

export default function TrialApp() {
  onButtonPress(BTN.CIRCLE, () => {
    for (;;) {
      // Deliberate watchdog fault for the hardware rollback gate.
    }
  });

  return (
    <View class="w-full h-full flex-col p-4 bg-slate-950">
      <Text class="text-lg text-white font-bold">POCKETJS</Text>
      <Text class="text-xs text-cyan-300">PENDING TRIAL</Text>

      <View class="flex-1 flex-col justify-center gap-2">
        <Text class="text-lg text-white">BETA</Text>
        <Text class="text-sm text-white">FIRST FRAME: OK</Text>
        <Text class="text-sm text-slate-400">Center - fault trial</Text>
      </View>

      <Text class="text-xs text-slate-400">Menu + Play - reboot</Text>
    </View>
  );
}
