import { Text, View } from "@pocketjs/framework/components";

export default function TargetApp(props: { name: string; ordinal: string }) {
  return (
    <View class="w-full h-full flex-col p-4 bg-slate-950">
      <Text class="text-lg text-white font-bold">POCKETJS</Text>
      <Text class="text-xs text-cyan-300">DISCOVERED APP</Text>

      <View class="flex-1 flex-col justify-center gap-2">
        <Text class="text-lg text-white">{props.name}</Text>
        <Text class="text-sm text-white">LAUNCH: OK</Text>
        <Text class="text-sm text-slate-400">{props.ordinal}</Text>
      </View>

      <Text class="text-xs text-slate-400">Menu + Play - reboot</Text>
    </View>
  );
}
