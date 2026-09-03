import {
  createWavPlayer,
  type WavPcm,
  type WavPlayer,
} from "@pocketjs/framework/audio";

const ui = (globalThis as { ui?: any }).ui;
if (!ui || ui.__host !== "ipod-photo" || ui.__hostAbi !== 1) {
  throw new Error("iPod Photo audio test requires host ABI 1");
}

const ROOT = ui.__root as number;
const PROP = {
  width: 1,
  height: 2,
  position: 24,
  top: 25,
  left: 28,
  background: 64,
  radius: 68,
  opacity: 69,
  borderColor: 70,
  borderWidth: 71,
} as const;
const ABSOLUTE = 1;
const BTN = {
  START: 0x0008,
  RIGHT: 0x0020,
  LEFT: 0x0080,
  TRIANGLE: 0x1000,
  CIRCLE: 0x2000,
} as const;

const abgr = (r: number, g: number, b: number, a = 255): number =>
  ((r & 255) | ((g & 255) << 8) | ((b & 255) << 16) | ((a & 255) << 24)) >>> 0;

const COLOR = {
  dark: abgr(24, 31, 43),
  track: [
    abgr(69, 225, 126),
    abgr(67, 198, 255),
    abgr(255, 210, 64),
    abgr(222, 87, 255),
  ],
  cyan: abgr(0, 222, 255),
  green: abgr(45, 235, 105),
  yellow: abgr(255, 218, 50),
  orange: abgr(255, 145, 35),
  red: abgr(255, 72, 88),
  purple: abgr(190, 95, 255),
  white: abgr(235, 241, 255),
};

function view(x: number, y: number, width: number, height: number, color: number): number {
  const node = ui.createNode(0) as number;
  if (!node) throw new Error("audio test UI node allocation failed");
  ui.setProp(node, PROP.position, ABSOLUTE);
  ui.setProp(node, PROP.left, x);
  ui.setProp(node, PROP.top, y);
  ui.setProp(node, PROP.width, width);
  ui.setProp(node, PROP.height, height);
  ui.setProp(node, PROP.background, color);
  ui.insertBefore(ROOT, node, 0);
  return node;
}

/* Build the qualification tones with TypedArray.fill() blocks rather than a
 * per-sample JavaScript loop. On the 80 MHz PP5020 this keeps package eval a
 * bounded cold path while preserving the same portable PCM formats. Stereo
 * streams intentionally carry the same square wave on both channels; channel
 * independence was already qualified in the native Campaign-4 mixer. */
function squarePcm(
  sampleRate: number,
  channels: 1 | 2,
  seconds: number,
  frequency: number,
  amplitude: number,
): WavPcm {
  const frames = Math.floor(sampleRate * seconds);
  const data = new Int16Array(frames * channels);
  const halfPeriod = Math.max(1, Math.floor(sampleRate / (frequency * 2)));
  let sample = amplitude;
  for (let first = 0; first < frames; first += halfPeriod) {
    const last = Math.min(frames, first + halfPeriod);
    data.fill(sample, first * channels, last * channels);
    sample = -sample;
  }
  const quietFrames = Math.min(32, Math.floor(frames / 4));
  data.fill(0, 0, quietFrames * channels);
  data.fill(0, (frames - quietFrames) * channels);
  return { sampleRate, channels, frames, data };
}

/* Keep the original four portable formats and durations. The bulk-fill tone
 * builder makes this ~1.2 MiB retained PCM set cheap to construct without
 * weakening the 44.1/22.05/11.025 kHz and mono/stereo hardware coverage. */
const pcm: readonly WavPcm[] = [
  squarePcm(44100, 2, 1.5, 330, 1500),
  squarePcm(22050, 1, 4.0, 550, 1450),
  squarePcm(11025, 2, 5.0, 660, 1400),
  squarePcm(44100, 1, 6.0, 880, 1350),
];
const players: WavPlayer[] = [
  createWavPlayer(),
  createWavPlayer(),
  createWavPlayer(),
  createWavPlayer(),
];
for (let index = 0; index < players.length; index++) {
  if (!players[index]!.loadPcm(pcm[index]!)) {
    throw new Error(`audio.pcm stream ${index} refused`);
  }
}

const tracks = players.map((_player, index) => {
  const y = 62 + index * 20;
  const back = view(10, y, 200, 11, COLOR.dark);
  ui.setProp(back, PROP.radius, 3);
  ui.setProp(back, PROP.borderWidth, 1);
  ui.setProp(back, PROP.borderColor, COLOR.white);
  const fill = view(11, y + 1, 1, 9, COLOR.track[index]!);
  ui.setProp(fill, PROP.radius, 2);
  return fill;
});

const masterBack = view(10, 145, 200, 7, COLOR.dark);
ui.setProp(masterBack, PROP.radius, 3);
const masterFill = view(10, 145, 130, 7, COLOR.cyan);
ui.setProp(masterFill, PROP.radius, 3);
const phaseChip = view(10, 158, 12, 9, COLOR.cyan);
ui.setProp(phaseChip, PROP.radius, 2);
const underrunChip = view(29, 158, 12, 9, COLOR.dark);
ui.setProp(underrunChip, PROP.radius, 2);
const readyChip = view(48, 158, 12, 9, COLOR.green);
ui.setProp(readyChip, PROP.radius, 2);

let master = 0.65;
const trackScale = [0.62, 0.55, 0.52, 0.55];
let scenarioTick = 0;
let pumpCursor = 0;
let previousButtons = 0;
let firstStart = true;
const D_STARVE_START = 60;  // first give D ~1 second to establish a full ring
const D_STARVE_END = 96;    // then withhold guest writes for ~0.6 second

function applyVolumes(): void {
  for (let index = 0; index < players.length; index++) {
    players[index]!.setVolume(master * trackScale[index]!);
  }
  ui.setProp(masterFill, PROP.width, Math.max(1, Math.floor(200 * master)));
}

function restartScenario(): void {
  scenarioTick = 0;
  pumpCursor = 0;
  trackScale[0] = 0.62;
  trackScale[1] = 0.55;
  trackScale[2] = 0.52;
  trackScale[3] = 0.55;
  for (const player of players) player.stop();
  applyVolumes();
  /* play() is intentionally cheap here. WavPlayer defers the native play op
   * until that stream receives its first bounded pump below. This removes the
   * old 16-pump first-frame burst while preserving underrun-safe startup. */
  for (const player of players) player.play();
  firstStart = false;
}

function replayC(): void {
  players[2]!.stop();
  players[2]!.play();
}

function updateVisuals(): void {
  for (let index = 0; index < players.length; index++) {
    const player = players[index]!;
    const duration = player.durationFrames();
    const position = player.positionFrames();
    const width = duration <= 0 ? 1 : Math.max(1, Math.min(198, Math.floor((198 * position) / duration)));
    ui.setProp(tracks[index]!, PROP.width, width);
    ui.setProp(tracks[index]!, PROP.opacity, player.playing() ? 1.0 : 0.28);
  }
  const dUnderruns = players[3]!.stats().underruns;
  ui.setProp(underrunChip, PROP.background, dUnderruns > 0 ? COLOR.red : COLOR.dark);

  let phase = COLOR.cyan;
  if (scenarioTick >= D_STARVE_START && scenarioTick < D_STARVE_END) phase = COLOR.orange;
  else if (scenarioTick >= 120 && scenarioTick < 180) phase = COLOR.yellow;
  else if (scenarioTick >= 210 && scenarioTick < 240) phase = COLOR.red;
  else if (scenarioTick >= 270 && scenarioTick < 300) phase = COLOR.purple;
  else if (scenarioTick >= 330 && scenarioTick < 350) phase = COLOR.green;
  ui.setProp(phaseChip, PROP.background, phase);
}

function buttonEdges(buttons: number): void {
  const pressed = buttons & ~previousButtons;
  previousButtons = buttons;
  if (pressed & BTN.START) restartScenario();
  if (pressed & BTN.CIRCLE) players[1]!.toggle();
  if (pressed & BTN.TRIANGLE) replayC();
  if (pressed & BTN.RIGHT) {
    master = Math.min(1, master + 0.05);
    applyVolumes();
  }
  if (pressed & BTN.LEFT) {
    master = Math.max(0.1, master - 0.05);
    applyVolumes();
  }
}

function pumpOneStream(): void {
  const index = pumpCursor;
  pumpCursor = (pumpCursor + 1) & 3;
  if (index === 3 && scenarioTick >= D_STARVE_START && scenarioTick < D_STARVE_END) {
    return;
  }
  /* One player per 60 Hz frame means every stream is serviced at 15 Hz.
   * A 4096-frame chunk is ~93 ms even at 44.1 kHz, safely longer than the
   * ~67 ms service interval, while keeping each QuickJS turn bounded. */
  players[index]!.pump();
}

applyVolumes();
updateVisuals();

(globalThis as { frame?: (buttons: number, analog?: number) => number }).frame =
  function frame(buttons: number): number {
    if (firstStart) restartScenario();
    scenarioTick++;
    buttonEdges(buttons >>> 0);

    if (scenarioTick === 120) players[1]!.pause();
    if (scenarioTick === 180) players[1]!.play();
    if (scenarioTick === 210) players[2]!.stop();
    if (scenarioTick === 240) players[2]!.play();
    if (scenarioTick === 270) {
      trackScale[3] = 0.22;
      applyVolumes();
    }
    if (scenarioTick === 300) {
      trackScale[3] = 0.55;
      applyVolumes();
    }
    if (scenarioTick === 330) {
      /* Replacing D destroys the old generation-tagged native handle and
       * creates a new one without touching A/B/C. Its next scheduled pump
       * feeds the new ring and opens playback. */
      if (!players[3]!.loadPcm(pcm[3]!)) throw new Error("D generation reload refused");
      players[3]!.setVolume(master * trackScale[3]!);
      players[3]!.play();
    }
    if (scenarioTick >= 600) restartScenario();

    pumpOneStream();
    if ((scenarioTick % 12) === 0) updateVisuals();
    return scenarioTick;
  };
