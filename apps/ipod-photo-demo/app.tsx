import { createSignal } from "solid-js";
import { Text, View } from "@pocketjs/framework/components";
import { BTN } from "@pocketjs/framework/input";
import { onButtonPress } from "@pocketjs/framework/lifecycle";

/**
 * Deliberately small target qualification app: three fixed, labelled gates.
 * It keeps the physical candidate legible at 220x176 while exercising the
 * ordinary framework compiler, packer, baked text, and input path.
 */
export default function IpodPhotoQualification() {
  const [selectStatus, setSelectStatus] = createSignal("WAIT");
  const [wheelStatus, setWheelStatus] = createSignal("WAIT");
  onButtonPress(BTN.CIRCLE, () => setSelectStatus("OK"));
  onButtonPress(BTN.LEFT | BTN.RIGHT, () => setWheelStatus("OK"));

  return (
    <View class="w-full h-full flex-col p-4 bg-slate-950">
      <Text class="text-lg text-white font-bold">POCKETJS</Text>
      <Text class="text-xs text-cyan-300">IPOD PHOTO - ORDINARY APP</Text>

      <View class="flex-1 flex-col justify-center gap-2">
        <Text class="text-sm text-white">APP: OK</Text>
        <Text class="text-sm text-white">SELECT: {selectStatus()}</Text>
        <Text class="text-sm text-white">WHEEL: {wheelStatus()}</Text>
      </View>

      <Text class="text-xs text-slate-400">Press center - turn wheel</Text>
    </View>
  );
}
