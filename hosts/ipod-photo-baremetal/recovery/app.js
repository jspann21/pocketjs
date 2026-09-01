(() => {
  "use strict";

  const ui = globalThis.ui;
  if (!ui || ui.__host !== "ipod-photo" || ui.__hostAbi !== 1) {
    throw new Error("PocketJS A1099 host ABI mismatch");
  }

  const ROOT = ui.__root;
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
  };
  const ABSOLUTE = 1;
  const ANALOG_CENTER = 0x8080;
  const BTN = {
    START: 0x0008,
    RIGHT: 0x0020,
    LEFT: 0x0080,
    TRIANGLE: 0x1000,
    CIRCLE: 0x2000,
  };
  const ipodInput = ui.__ipodInput;
  if (!ipodInput) throw new Error("PocketJS iPod input side channel missing");

  const abgr = (r, g, b, a = 255) =>
    ((r & 255) | ((g & 255) << 8) | ((b & 255) << 16) | ((a & 255) << 24)) >>> 0;

  function view(x, y, width, height, color) {
    const node = ui.createNode(0);
    if (!node) throw new Error("PocketJS node allocation failed");
    ui.setProp(node, PROP.position, ABSOLUTE);
    ui.setProp(node, PROP.left, x);
    ui.setProp(node, PROP.top, y);
    ui.setProp(node, PROP.width, width);
    ui.setProp(node, PROP.height, height);
    ui.setProp(node, PROP.background, color);
    ui.insertBefore(ROOT, node, 0);
    return node;
  }

  function setPropBatch(records) {
    const values = new Float64Array(records.length * 3);
    for (let index = 0; index < records.length; ++index) {
      values[index * 3] = records[index][0];
      values[index * 3 + 1] = records[index][1];
      values[index * 3 + 2] = records[index][2];
    }
    /* Pass the typed-array view, rather than its backing buffer, to exercise
     * the offset/length-aware QuickJS HostOp path used by framework batches. */
    ui.setPropBatch(values);
  }

  /* This lane is guest-owned. It is deliberately placed in the unused strip
   * between the native power row and Hold overlay, so its presence proves that
   * the embedded .pocket package was admitted, evaluated by QuickJS, and
   * connected to the retained PocketJS UI through HostOps. */
  const lane = view(8, 39, 204, 6, abgr(34, 43, 59));
  ui.setProp(lane, PROP.radius, 3);
  ui.setProp(lane, PROP.borderWidth, 1);
  ui.setProp(lane, PROP.borderColor, abgr(82, 103, 142));

  const marker = ui.createNode(0);
  if (!marker) throw new Error("PocketJS marker allocation failed");
  setPropBatch([
    [marker, PROP.position, ABSOLUTE],
    [marker, PROP.left, 8],
    [marker, PROP.top, 36],
    [marker, PROP.width, 10],
    [marker, PROP.height, 12],
    [marker, PROP.background, abgr(238, 76, 255)],
  ]);
  ui.setProp(marker, PROP.radius, 3);
  ui.insertBefore(ROOT, marker, 0);

  const pulse = view(112, 4, 8, 8, abgr(238, 76, 255));
  ui.setProp(pulse, PROP.radius, 2);

  let ticks = 0;
  let lastX = -1;
  let lastColor = 0xffffffff;
  let lastOpacity = -1;
  let lastPulse = -1;

  /* Generated PocketJS bundles call frame(buttons, analog?, touches?, hits?,
   * touchSurfaces?).  The A1099 has no nub or touch panel, so the host passes
   * only the standard button mask and centered analog word.  Target-local
   * wheel/Hold/telemetry facts come from the private side channel above. */
  globalThis.frame = function frame(buttons, analog) {
    ticks = (ticks + 1) >>> 0;

    const wheelDelta = ipodInput.wheelDelta | 0;
    const wheelPosition = ipodInput.wheelPosition >>> 0;
    const wheelTouched = !!ipodInput.wheelTouched;
    const hold = !!ipodInput.hold;

    const position = Math.max(0, Math.min(95, wheelPosition | 0));
    const x = 8 + Math.floor((position * 194) / 95);
    if (x !== lastX) {
      ui.setProp(marker, PROP.left, x);
      lastX = x;
    }

    let color = abgr(238, 76, 255);
    if (hold) color = abgr(255, 78, 90);
    else if (wheelTouched) color = abgr(0, 222, 255);
    else if (buttons & BTN.CIRCLE) color = abgr(45, 235, 105); // Select
    else if (buttons & BTN.TRIANGLE) color = abgr(255, 92, 108); // Menu
    else if (buttons & BTN.START) color = abgr(255, 218, 50); // Play
    else if (wheelDelta) color = abgr(120, 164, 255);
    else if (analog !== ANALOG_CENTER) color = abgr(255, 145, 35);
    if (color !== lastColor) {
      ui.setProp(marker, PROP.background, color);
      lastColor = color;
    }

    const opacity = hold ? 0.45 : 1.0;
    if (opacity !== lastOpacity) {
      ui.setProp(marker, PROP.opacity, opacity);
      lastOpacity = opacity;
    }

    const pulsePhase = (ticks >>> 4) & 1;
    if (pulsePhase !== lastPulse) {
      ui.setProp(pulse, PROP.opacity, pulsePhase ? 1.0 : 0.35);
      lastPulse = pulsePhase;
    }

    return ticks;
  };
})();
