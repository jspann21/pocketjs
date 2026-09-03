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

function squarePcm(
  sampleRate: number,
  channels: 1 | 2,
  seconds: number,
  leftHz: number,
  rightHz: number,
  amplitude: number,
): WavPcm {
  const frames = Math.floor(sampleRate * seconds);
  const data = new Int16Array(frames * channels);
  const leftPeriod = Math.max(2, Math.floor(sampleRate / leftHz));
  const rightPeriod = Math.max(2, Math.floor(sampleRate / rightHz));
  for (let frame = 0; frame < frames; frame++) {
    // A short integer ramp suppresses the worst start/end click without a
    // floating-point sine table in the 80 MHz QuickJS boot path.
    let gain = amplitude;
    const tail = frames - 1 - frame;
    if (frame < 64) gain = Math.floor((gain * frame) / 64);
    else if (tail < 64) gain = Math.floor((gain * tail) / 64);
    const left = frame % leftPeriod < leftPeriod / 2 ? gain : -gain;
    if (channels === 1) {
      data[frame] = left;
    } else {
      const right = frame % rightPeriod < rightPeriod / 2 ? gain : -gain;
      data[frame * 2] = left;
      data[frame * 2 + 1] = right;
    }
  }
  return { sampleRate, channels, frames, data };
}

/* Four deliberately different portable formats. Total retained PCM is about
 * 1.2 MiB, leaving substantial room below the A1099 QuickJS heap limit. */
const pcm: readonly WavPcm[] = [
  squarePcm(44100, 2, 1.5, 330, 440, 1500),
  squarePcm(22050, 1, 4.0, 550, 550, 1450),
  squarePcm(11025, 2, 5.0, 660, 770, 1400),
  squarePcm(44100, 1, 6.0, 880, 880, 1350),
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
let dResumeTick = 0;
let previousButtons = 0;
let firstStart = true;

function applyVolumes(): void {
  for (let index = 0; index < players.length; index++) {
    players[index]!.setVolume(master * trackScale[index]!);
  }
  ui.setProp(masterFill, PROP.width, Math.max(1, Math.floor(200 * master)));
}

function prime(player: WavPlayer): void {
  // Four 4096-frame pumps fill the contract's 16,384-frame source ring.
  // This makes stream D's intentional no-pump interval a real starvation
  // recovery test rather than a startup underrun.
  for (let index = 0; index < 4; index++) player.pump();
}

function restartScenario(): void {
  scenarioTick = 0;
  trackScale[0] = 0.62;
  trackScale[1] = 0.55;
  trackScale[2] = 0.52;
  trackScale[3] = 0.55;
  for (const player of players) player.stop();
  applyVolumes();
  for (const player of players) {
    player.play();
    prime(player);
  }
  // D is now fully primed, then intentionally receives no guest pump for
  // 36 ticks (~0.6 s). Its 16k ring empties at ~0.37 s; A/B/C continue.
  // Another player drains the shared native poll queue first, so D recovering
  // also proves the framework's per-handle event broker.
  dResumeTick = 36;
  firstStart = false;
}

function replayC(): void {
  players[2]!.stop();
  players[2]!.play();
  prime(players[2]!);
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
  if (scenarioTick < dResumeTick) phase = COLOR.orange; // deliberate D starvation
  else if (scenarioTick >= 120 && scenarioTick < 180) phase = COLOR.yellow; // B paused
  else if (scenarioTick >= 210 && scenarioTick < 240) phase = COLOR.red; // C stopped
  else if (scenarioTick >= 270 && scenarioTick < 300) phase = COLOR.purple; // D low gain
  else if (scenarioTick >= 330 && scenarioTick < 350) phase = COLOR.green; // D generation swap
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

applyVolumes();
updateVisuals();

(globalThis as { frame?: (buttons: number, analog?: number) => number }).frame =
  function frame(buttons: number): number {
    if (firstStart) restartScenario();
    scenarioTick++;
    buttonEdges(buttons >>> 0);

    // Deterministic independent-control timeline. Manual controls may alter a
    // stream between these checkpoints; the next checkpoint remains valid.
    if (scenarioTick === 120) players[1]!.pause();
    if (scenarioTick === 180) players[1]!.play();
    if (scenarioTick === 210) players[2]!.stop();
    if (scenarioTick === 240) {
      players[2]!.play();
      prime(players[2]!);
    }
    if (scenarioTick === 270) {
      trackScale[3] = 0.22;
      applyVolumes();
    }
    if (scenarioTick === 300) {
      trackScale[3] = 0.55;
      applyVolumes();
    }
    if (scenarioTick === 330) {
      // Replacing a still-live WavPlayer source destroys the old native handle
      // and creates a new generation while A/B/C remain untouched.
      if (!players[3]!.loadPcm(pcm[3]!)) throw new Error("D generation reload refused");
      players[3]!.setVolume(master * trackScale[3]!);
      players[3]!.play();
      prime(players[3]!);
    }
    if (scenarioTick >= 600) restartScenario();

    // A always drains the namespace first. During D's skip interval this is
    // intentional: the framework router must preserve D's underrun/credit
    // facts until D resumes pumping.
    players[0]!.pump();
    players[1]!.pump();
    players[2]!.pump();
    if (scenarioTick >= dResumeTick) players[3]!.pump();

    if ((scenarioTick % 6) === 0) updateVisuals(); // localized 10 Hz damage
    return scenarioTick;
  };
