// Instruments: a logic analyser and an oscilloscope on the pins of the emulated MCU.
//
// The C++ side records every pin change with the clock cycle it happened on (SignalRecorder) and serves the capture as
// typed arrays through sf.signals. Nothing is sampled: an edge is where the firmware put it, to the cycle, whatever
// the speed of the emulation. This module keeps the capture, draws it on a canvas (digital lanes or voltages), and
// measures it: frequency, duty cycle, voltages, cursors, a trigger, and a UART decoder.
"use strict";

window.SiliconForgeInstruments = (root, host) => {
  const $ = (selector) => root.querySelector(selector);
  const canvas = $("canvas");
  // Trace colours, as on an oscilloscope: bright on the dark screen, darker on the light one.
  const palettes = {
    dark: ["#f5d90a", "#35c5f0", "#f06bd3", "#5fd35f", "#ff9f45", "#8ea2ff", "#ff6b6b", "#b9f18c", "#c79bff", "#7fe0d0"],
    light: ["#b58900", "#0b84c9", "#c2349b", "#2a9d3f", "#d9730d", "#4a5bd6", "#d1344a", "#5c8f1f", "#7c4ddb", "#1a9c8b"],
  };
  let palette = palettes.dark;
  const maxSamples = 200000;  // Per channel; the older half is dropped beyond.
  const supply = 5;
  const bauds = [300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 250000, 500000, 1000000];

  // Capture --------------------------------------------------------------------------------------------------------
  const capture = {
    epoch: -1, cursor: 0, now: 0, clockHz: 16e6, lost: false,
    channels: new Map(),  // Pin index -> {t: seconds[], v: volts[], s: states[]} in time order
  };
  let polling = null;

  const channel = (index) => {
    let entry = capture.channels.get(index);
    if (!entry) capture.channels.set(index, entry = {t: [], v: [], s: []});
    return entry;
  };

  /** Fetches what was recorded since the last call. Resolves once the page holds the whole capture. */
  function poll() {
    if (polling) return polling;
    polling = (async () => {
      for (;;) {
        const [epoch, first, next, now, at, pins, volts, states] = await host.call("sf.signals", capture.epoch, capture.cursor);
        if (epoch !== capture.epoch) {
          capture.epoch = epoch;
          capture.channels.clear();
          capture.lost = false;
          view.triggered = null;
        } else if (first > capture.cursor) {
          capture.lost = true;  // The ring overtook this page: there is a hole in what it holds.
        }
        capture.clockHz = host.state?.clockHz ?? capture.clockHz;
        for (let i = 0; i < at.length; ++i) {
          const entry = channel(pins[i]);
          entry.t.push(at[i] / capture.clockHz);
          entry.v.push(volts[i]);
          entry.s.push(states[i]);
          if (entry.t.length > maxSamples) for (const list of [entry.t, entry.v, entry.s]) list.splice(0, maxSamples / 2);
        }
        capture.cursor = next;
        capture.now = now / capture.clockHz;
        if (at.length < 32 * 1024) break;
      }
    })().finally(() => { polling = null; draw(); });
    return polling;
  }

  /** Index of the last sample at or before `time`, -1 when there is none. */
  const indexAt = (entry, time) => {
    let low = 0;
    let high = entry.t.length;
    while (low < high) {
      const middle = (low + high) >> 1;
      if (entry.t[middle] <= time) low = middle + 1; else high = middle;
    }
    return low - 1;
  };

  const pinIndex = (name) => host.state?.pins.findIndex((pin) => pin.name === name || pin.label === name) ?? -1;
  const pinName = (index) => {
    const pin = host.state?.pins[index];
    return pin ? (pin.label || pin.name) : `#${index}`;
  };

  // Measurements ---------------------------------------------------------------------------------------------------
  /**
   * What an instrument reads on one channel between two instants (the whole capture by default): frequency and duty
   * cycle from the rising edges, voltages weighted by time. Fields that cannot be measured are null.
   */
  function measure(name, from = -Infinity, to = Infinity) {
    const entry = capture.channels.get(typeof name === "number" ? name : pinIndex(name));
    const result = {edges: 0, frequency: null, period: null, duty: null, vmin: null, vmax: null, vavg: null, high: null, state: null};
    if (!entry || !entry.t.length) return result;
    const end = Math.min(to, capture.now);
    let i = Math.max(0, indexAt(entry, from));
    const start = Math.max(from, entry.t[i]);
    if (end <= start) return result;
    let firstRise = null, lastRise = null, rises = 0, highBetween = 0, pendingHigh = 0, area = 0;
    let vmin = Infinity, vmax = -Infinity;
    for (; i < entry.t.length && entry.t[i] <= end; ++i) {
      const begin = Math.max(entry.t[i], start);
      const until = Math.min(i + 1 < entry.t.length ? entry.t[i + 1] : end, end);
      const isHigh = entry.s[i] === 1;
      area += entry.v[i] * (until - begin);
      vmin = Math.min(vmin, entry.v[i]);
      vmax = Math.max(vmax, entry.v[i]);
      if (entry.t[i] >= start && isHigh && i > 0 && entry.s[i - 1] !== 1) {
        if (firstRise === null) firstRise = entry.t[i];
        else highBetween += pendingHigh;
        pendingHigh = 0;
        lastRise = entry.t[i];
        ++rises;
      }
      if (entry.t[i] >= start && i > 0 && (entry.s[i] === 1) !== (entry.s[i - 1] === 1)) ++result.edges;
      if (isHigh && firstRise !== null) pendingHigh += until - begin;
    }
    result.vmin = vmin;
    result.vmax = vmax;
    result.vavg = area / (end - start);
    result.state = entry.s[Math.min(i, entry.t.length) - 1];
    result.high = result.state === 1;
    if (rises >= 2) {
      result.period = (lastRise - firstRise) / (rises - 1);
      result.frequency = 1 / result.period;
      result.duty = highBetween / (lastRise - firstRise);
    }
    return result;
  }

  /**
   * The standard rate whose bit time is the shortest pulse width of a channel, or null when it has no pulse. A width
   * counts once it was seen three times, so that a lone glitch (the line settling when the transmitter is enabled)
   * is not taken for a bit.
   */
  function guessBaud(entry) {
    const widths = [];
    for (let i = Math.max(1, entry.t.length - 4000); i < entry.t.length; ++i)
      if (entry.s[i] !== entry.s[i - 1] && entry.t[i] > entry.t[i - 1]) widths.push(entry.t[i] - entry.t[i - 1]);
    if (!widths.length) return null;
    widths.sort((a, b) => a - b);
    let bit = widths[0];
    for (let i = 0; i + 2 < widths.length; ++i) if (widths[i + 2] <= widths[i] * 1.05) { bit = widths[i]; break; }
    return bauds.reduce((best, rate) => (Math.abs(Math.log(rate * bit)) < Math.abs(Math.log(best * bit)) ? rate : best));
  }

  /**
   * Decodes asynchronous serial frames (8 data bits, no parity, one stop bit, idle high) from the edges of a channel,
   * sampling each bit at its middle like a receiver. Returns {baud, bytes: [{t, end, value, ok}], text}.
   */
  function decodeUart(name, baud = 0, from = -Infinity, to = Infinity) {
    const entry = capture.channels.get(typeof name === "number" ? name : pinIndex(name));
    const result = {baud: null, bytes: [], text: ""};
    if (!entry || entry.t.length < 2) return result;
    result.baud = baud || guessBaud(entry);
    if (!result.baud) return result;
    const bit = 1 / result.baud;
    const high = (time) => { const i = indexAt(entry, time); return i < 0 || entry.s[i] !== 0; };
    let free = -Infinity;
    for (let i = Math.max(1, indexAt(entry, from - 12 * bit)); i < entry.t.length && entry.t[i] <= to; ++i) {
      const start = entry.t[i];
      if (entry.s[i] !== 0 || entry.s[i - 1] === 0 || start < free) continue;  // Not a start bit
      if (start + 9.5 * bit > capture.now) break;  // The frame is still being sent
      let value = 0;
      for (let k = 0; k < 8; ++k) if (high(start + (1.5 + k) * bit)) value |= 1 << k;
      const ok = high(start + 9.5 * bit);
      result.bytes.push({t: start, end: start + 10 * bit, value, ok});
      free = start + 9.5 * bit;
    }
    result.text = String.fromCharCode(...result.bytes.map((byte) => byte.value));
    return result;
  }

  /** The capture as a Value Change Dump, for GTKWave, PulseView or Surfer. One time unit is one clock cycle. */
  function vcd(indexes = shown()) {
    const period = 1e12 / capture.clockHz;  // Picoseconds per cycle
    const unit = Number.isInteger(period / 1000) ? [period / 1000, "ns"] : [Math.round(period), "ps"];
    const id = (k) => String.fromCharCode(33 + k);
    const lines = ["$comment SiliconForge capture: one time unit is one clock cycle $end", `$timescale ${unit[0]} ${unit[1]} $end`, "$scope module mcu $end"];
    indexes.forEach((index, k) => lines.push(`$var wire 1 ${id(k)} ${pinName(index).replace(/\s/g, "_")} $end`));
    lines.push("$upscope $end", "$enddefinitions $end");
    const events = [];
    indexes.forEach((index, k) => {
      const entry = capture.channels.get(index);
      if (!entry) return;
      for (let i = 0; i < entry.t.length; ++i)
        if (i === 0 || entry.s[i] !== entry.s[i - 1]) events.push([Math.round(entry.t[i] * capture.clockHz), "01z"[entry.s[i]] + id(k)]);
    });
    events.sort((a, b) => a[0] - b[0]);
    let time = null;
    for (const [at, change] of events) {
      if (at !== time) lines.push(`#${time = at}`);
      lines.push(change);
    }
    lines.push(`#${Math.round(capture.now * capture.clockHz)}`);
    return lines.join("\n") + "\n";
  }

  // View -----------------------------------------------------------------------------------------------------------
  const view = {
    mode: "logic",      // "logic": digital lanes, "scope": voltages
    span: 0.002,        // Seconds across the plot
    right: 0,           // Instant at the right edge when not following the capture
    live: true,
    auto: true,         // Channels chosen from the circuit and the activity
    channels: [],       // Pin indexes shown when not automatic
    trigger: {source: -1, rising: true, single: false},
    triggered: null,    // Instant of the trigger shown
    uart: {source: -1, baud: 0},
    cursors: [null, null],
  };
  const gutter = {left: 78, right: 168, bottom: 22, top: 6};

  /** Channels displayed: those the user picked or, automatically, the pins of the circuit and the ones that move. */
  function shown() {
    if (!view.auto) return view.channels.filter((index) => host.state?.pins[index]);
    const wanted = new Set();
    for (const component of host.circuit?.components ?? []) {
      const index = component.pin ? pinIndex(component.pin) : -1;
      if (index >= 0 && Object.keys(component.state ?? {}).length) wanted.add(index);
    }
    for (const [index, entry] of capture.channels) if (entry.t.length > 2) wanted.add(index);
    const labels = new Intl.Collator(undefined, {numeric: true});
    return [...wanted].sort((a, b) => labels.compare(pinName(a), pinName(b))).slice(0, 10);
  }

  const si = (value, unit, digits = 3) => {
    if (value === null || !Number.isFinite(value)) return "—";
    const magnitude = Math.abs(value);
    const [scale, prefix] = magnitude >= 1e6 ? [1e-6, "M"] : magnitude >= 1e3 ? [1e-3, "k"] : magnitude >= 1 || magnitude === 0 ? [1, ""]
      : magnitude >= 1e-3 ? [1e3, "m"] : magnitude >= 1e-6 ? [1e6, "µ"] : [1e9, "n"];
    return `${Number((value * scale).toPrecision(digits))} ${prefix}${unit}`;
  };

  /** Left and right instants of the plot. A trigger freezes the window on the last complete match. */
  function timeWindow() {
    const pre = 0.25;  // Fraction of the plot before the trigger
    if (view.live) {
      const entry = capture.channels.get(view.trigger.source);
      if (entry) {
        const latest = capture.now - view.span * (1 - pre);
        let i = indexAt(entry, latest);
        for (; i > 0; --i) if ((entry.s[i] === 1) === view.trigger.rising && (entry.s[i - 1] === 1) !== view.trigger.rising) break;
        if (i > 0 && entry.t[i] - view.span * pre >= entry.t[0]) {
          view.triggered = entry.t[i];
          if (view.trigger.single) {
            view.live = false;
            view.right = view.triggered + view.span * (1 - pre);
            syncControls();
          }
        }
        if (view.triggered !== null) return [view.triggered - view.span * pre, view.triggered + view.span * (1 - pre)];
      }
      view.triggered = null;
      return [capture.now - view.span, capture.now];
    }
    return [view.right - view.span, view.right];
  }

  let colours = {};
  function readColours() {
    const style = getComputedStyle(root);
    const read = (name, fallback) => style.getPropertyValue(name).trim() || fallback;
    palette = palettes[document.documentElement.dataset.theme === "light" ? "light" : "dark"];
    colours = {
      background: read("--scope-bg", "#0b0f14"), grid: read("--scope-grid", "#1f2933"), text: read("--scope-text", "#9aa7b4"),
      strong: read("--scope-strong", "#e6edf3"), cursor: read("--scope-cursor", "#ffffff"), trigger: read("--accent", "#f0a73a"),
    };
  }

  let pending = false;
  function draw() {
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; paint(); });
  }

  function paint() {
    if (!canvas.clientWidth || !canvas.clientHeight) return;  // Hidden
    const ratio = window.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
      canvas.width = Math.round(width * ratio);
      canvas.height = Math.round(height * ratio);
    }
    const g = canvas.getContext("2d");
    g.setTransform(ratio, 0, 0, ratio, 0, 0);
    g.fillStyle = colours.background;
    g.fillRect(0, 0, width, height);
    const plot = {x: gutter.left, y: gutter.top, w: Math.max(50, width - gutter.left - gutter.right), h: Math.max(40, height - gutter.top - gutter.bottom)};
    const [t0, t1] = timeWindow();
    const x = (time) => plot.x + ((time - t0) / (t1 - t0)) * plot.w;
    const indexes = shown();
    g.font = "11px ui-monospace, 'JetBrains Mono', Menlo, Consolas, monospace";
    g.textBaseline = "middle";

    // Time grid: ten divisions, labelled with the simulated time.
    const step = (t1 - t0) / 10;
    g.strokeStyle = colours.grid;
    g.lineWidth = 1;
    g.beginPath();
    for (let k = 0; k <= 10; ++k) {
      const px = Math.round(plot.x + (k / 10) * plot.w) + 0.5;
      g.moveTo(px, plot.y);
      g.lineTo(px, plot.y + plot.h);
    }
    g.stroke();
    g.fillStyle = colours.text;
    g.textAlign = "center";
    for (let k = 0; k <= 10; k += 2) {
      const time = t0 + k * step;
      const label = view.triggered !== null && view.live || view.triggered !== null && view.trigger.single
        ? si(time - view.triggered, "s") : `${time.toFixed(step < 1e-4 ? 6 : step < 0.1 ? 4 : 2)} s`;
      g.fillText(label, plot.x + (k / 10) * plot.w, plot.y + plot.h + 12);
    }

    // The lane of the channel being decoded is twice as high: it also holds the decoded bytes.
    const weights = indexes.map((index) => (view.uart.source === index ? 2 : 1));
    const unit = plot.h / Math.max(1, weights.reduce((sum, weight) => sum + weight, 0));
    let nextTop = plot.y;
    indexes.forEach((index, row) => {
      const entry = capture.channels.get(index);
      const colour = palette[row % palette.length];
      const lane = unit * weights[row];
      const top = nextTop;
      nextTop += lane;
      const decoding = view.uart.source === index;
      const signalBottom = top + lane - (decoding ? Math.min(20, lane * 0.45) : 0) - 5;
      const signalTop = top + 6;
      const yOf = view.mode === "scope"
        ? (volts) => signalBottom - (Math.min(supply * 1.1, Math.max(-0.2, volts)) / supply) * (signalBottom - signalTop) * 0.9
        : (volts, state) => (state === 2 ? (signalTop + signalBottom) / 2 : state === 1 ? signalTop : signalBottom);

      g.strokeStyle = colours.grid;
      g.beginPath();
      g.moveTo(plot.x, Math.round(top + lane) + 0.5);
      g.lineTo(plot.x + plot.w, Math.round(top + lane) + 0.5);
      if (view.mode === "scope") {  // 0 V and supply references
        g.setLineDash([2, 4]);
        for (const volts of [0, supply]) { g.moveTo(plot.x, yOf(volts) + 0.5); g.lineTo(plot.x + plot.w, yOf(volts) + 0.5); }
      }
      g.stroke();
      g.setLineDash([]);

      // Name, and the scale in scope mode.
      g.fillStyle = colour;
      g.fillRect(4, top + 5, 3, lane - 10);
      g.textAlign = "left";
      g.fillStyle = colours.strong;
      g.font = "600 12px ui-monospace, 'JetBrains Mono', Menlo, Consolas, monospace";
      g.fillText(pinName(index), 13, top + lane / 2);
      g.font = "11px ui-monospace, 'JetBrains Mono', Menlo, Consolas, monospace";
      if (view.mode === "scope" && lane > 44) {
        g.fillStyle = colours.text;
        g.textAlign = "right";
        g.fillText(`${supply} V`, plot.x - 5, yOf(supply));
        g.fillText("0 V", plot.x - 5, yOf(0));
      }

      if (entry && entry.t.length) {
        // One vertical span per pixel column: a column that holds several edges is drawn as a solid bar, so a signal
        // too fast for the zoom looks like a band instead of aliasing.
        g.save();
        g.beginPath();
        g.rect(plot.x, top, plot.w, lane);
        g.clip();
        g.strokeStyle = colour;
        g.lineWidth = 1.6;
        g.lineJoin = "round";
        g.beginPath();
        const end = Math.min(t1, capture.now);
        const perPixel = (t1 - t0) / plot.w;
        const y = (k) => yOf(entry.v[k], entry.s[k]);
        let i = indexAt(entry, t0);
        if (i >= 0) g.moveTo(plot.x, y(i));
        for (;;) {
          const n = i + 1;
          if (n >= entry.t.length || entry.t[n] > end) break;
          const px = x(entry.t[n]);
          if (i < 0) { g.moveTo(px, y(n)); i = n; continue; }
          let j = n;
          const columnEnd = Math.min(entry.t[n] + perPixel, end);
          if (n + 1 < entry.t.length && entry.t[n + 1] <= columnEnd) j = indexAt(entry, columnEnd);
          g.lineTo(px, y(i));
          if (j > n) {
            let low = Infinity, high = -Infinity;
            for (let k = i; k <= j && k <= i + 64; ++k) { low = Math.min(low, y(k)); high = Math.max(high, y(k)); }
            if (j - i > 64 && view.mode !== "scope") { low = signalTop; high = signalBottom; }
            g.lineTo(px, low);
            g.lineTo(px, high);
            g.lineTo(px + 1, y(j));
          } else {
            g.lineTo(px, y(n));
          }
          i = j;
        }
        if (i >= 0) g.lineTo(Math.max(plot.x, Math.min(x(end), plot.x + plot.w)), y(i));
        g.stroke();

        if (decoding) {
          const decoded = decodeUart(index, view.uart.baud, t0, t1);
          const boxTop = signalBottom + 5;
          const boxHeight = top + lane - boxTop - 2;
          g.textAlign = "center";
          for (const byte of decoded.bytes) {
            const from = x(byte.t);
            const to = x(byte.end);
            if (to < plot.x || from > plot.x + plot.w) continue;
            g.fillStyle = byte.ok ? "rgba(53, 197, 240, .22)" : "rgba(248, 81, 73, .3)";
            g.strokeStyle = byte.ok ? "#35c5f0" : "#f85149";
            g.lineWidth = 1;
            g.fillRect(from, boxTop, Math.max(1, to - from - 1), boxHeight);
            g.strokeRect(from + 0.5, boxTop + 0.5, Math.max(1, to - from - 1), boxHeight - 1);
            if (to - from > 16) {
              const printable = byte.value >= 0x20 && byte.value < 0x7F;
              const text = to - from > 46 && printable ? `'${String.fromCharCode(byte.value)}' ${byte.value.toString(16).toUpperCase().padStart(2, "0")}`
                : printable ? String.fromCharCode(byte.value) : byte.value.toString(16).toUpperCase().padStart(2, "0");
              g.fillStyle = colours.strong;
              g.fillText(text, (from + to) / 2, boxTop + boxHeight / 2 + 0.5);
            }
          }
        }
        g.restore();
      }

      // What the instrument reads over the window.
      const m = measure(index, t0, t1);
      g.textAlign = "left";
      const mx = plot.x + plot.w + 8;
      g.fillStyle = colours.strong;
      const first = m.frequency !== null ? `${si(m.frequency, "Hz", 4)}  ${(m.duty * 100).toFixed(1)} %`
        : m.vmin === null ? "no data" : view.mode === "scope" ? `${m.vavg.toFixed(2)} V`
        : `${m.state === 2 ? "floating" : m.high ? "high" : "low"}${m.edges ? `  ${m.edges} edge${m.edges > 1 ? "s" : ""}` : ""}`;
      const second = m.frequency !== null ? (view.mode === "scope" ? `avg ${m.vavg.toFixed(2)} V  pp ${(m.vmax - m.vmin).toFixed(1)} V` : `T ${si(m.period, "s", 4)}`)
        : decoding && m.vmin !== null ? `${decodeUart(index, view.uart.baud, t0, t1).baud ?? "?"} baud` : "";
      if (lane > 30 && second) {
        g.fillText(first, mx, top + lane / 2 - 7);
        g.fillStyle = colours.text;
        g.fillText(second, mx, top + lane / 2 + 7);
      } else {
        g.fillText(first, mx, top + lane / 2);
      }
    });

    if (!indexes.length) {
      g.fillStyle = colours.text;
      g.textAlign = "center";
      g.fillText("No signal yet: run a firmware, or pick channels.", plot.x + plot.w / 2, plot.y + plot.h / 2);
    }

    // Trigger instant and cursors.
    const marker = (time, colour, label) => {
      const px = x(time);
      if (px < plot.x || px > plot.x + plot.w) return;
      g.strokeStyle = colour;
      g.lineWidth = 1;
      g.setLineDash([5, 3]);
      g.beginPath();
      g.moveTo(Math.round(px) + 0.5, plot.y);
      g.lineTo(Math.round(px) + 0.5, plot.y + plot.h);
      g.stroke();
      g.setLineDash([]);
      g.fillStyle = colour;
      g.textAlign = "center";
      g.fillText(label, px, plot.y + 7);
    };
    if (view.triggered !== null) marker(view.triggered, colours.trigger, "T");
    view.cursors.forEach((time, k) => { if (time !== null) marker(time, colours.cursor, "AB"[k]); });

    g.strokeStyle = colours.grid;
    g.strokeRect(plot.x + 0.5, plot.y + 0.5, plot.w, plot.h);
    painted = {plot, t0, t1};

    $("[data-readout=timebase]").textContent = `${si((t1 - t0) / 10, "s")}/div`;
    const [a, b] = view.cursors;
    $("[data-readout=cursors]").textContent = a !== null && b !== null
      ? `Δt ${si(Math.abs(b - a), "s", 4)}   1/Δt ${si(1 / Math.abs(b - a), "Hz", 4)}`
      : a !== null ? `A ${a.toFixed(6)} s — shift-click places B` : "click places cursor A, shift-click B";
    $("[data-readout=capture]").textContent = capture.lost ? "capture truncated"
      : performance.now() - exported < 6000 ? "siliconforge-capture.vcd saved to your downloads" : "";
  }
  let painted = null;

  // Controls -------------------------------------------------------------------------------------------------------
  function syncControls() {
    for (const tab of root.querySelectorAll("[data-mode]")) {
      tab.classList.toggle("selected", tab.dataset.mode === view.mode);
      tab.setAttribute("aria-selected", tab.dataset.mode === view.mode);
    }
    $("[data-action=live]").classList.toggle("active", view.live);
    $("[data-action=live]").textContent = view.live ? "Live" : "Held";
    $("[data-action=single]").classList.toggle("active", view.trigger.single);
    $("[data-select=edge]").value = view.trigger.rising ? "rising" : "falling";
    if ([...$("[data-select=trigger]").options].some((option) => option.value === String(view.trigger.source))) $("[data-select=trigger]").value = String(view.trigger.source);
    if ([...$("[data-select=uart]").options].some((option) => option.value === String(view.uart.source))) $("[data-select=uart]").value = String(view.uart.source);
    $("[data-select=baud]").value = String(view.uart.baud);
  }

  /** Rebuilds the pin lists of the selectors when the chip changes. */
  let listed = "";
  function listPins() {
    const pins = host.state?.pins ?? [];
    const key = `${host.state?.chipRevision}:${pins.length}:${view.auto}:${view.channels.join()}`;
    if (key === listed) return;
    listed = key;
    const labels = new Intl.Collator(undefined, {numeric: true});
    const order = pins.map((pin, index) => ({index, name: pin.label || pin.name})).sort((a, b) => labels.compare(a.name, b.name));
    for (const [selector, current, none] of [["[data-select=trigger]", view.trigger.source, "Trigger: off"], ["[data-select=uart]", view.uart.source, "UART: off"]]) {
      const select = $(selector);
      select.replaceChildren(new Option(none, "-1"), ...order.map(({index, name}) => new Option(`${none.split(":")[0]}: ${name}`, String(index))));
      select.value = String(pins[current] ? current : -1);
    }
    const list = $("[data-list=channels]");
    list.replaceChildren();
    const automatic = document.createElement("label");
    automatic.innerHTML = `<input type="checkbox" data-channel="auto"> <b>Automatic</b>`;
    automatic.firstChild.checked = view.auto;
    list.append(automatic);
    for (const {index, name} of order) {
      const label = document.createElement("label");
      const box = document.createElement("input");
      box.type = "checkbox";
      box.dataset.channel = index;
      box.checked = view.auto ? shown().includes(index) : view.channels.includes(index);
      label.append(box, ` ${name}`);
      list.append(label);
    }
  }

  const timeAt = (event) => {
    if (!painted) return null;
    const bounds = canvas.getBoundingClientRect();
    return painted.t0 + ((event.clientX - bounds.left - painted.plot.x) / painted.plot.w) * (painted.t1 - painted.t0);
  };
  const hold = () => {
    if (!view.live || !painted) return;
    view.live = false;
    view.right = painted.t1;
    syncControls();
  };
  const zoom = (factor, around = null) => {
    const span = Math.min(60, Math.max(2e-6, view.span * factor));
    if (around !== null && painted && !view.live) view.right = around + (painted.t1 - around) * (span / view.span);
    view.span = span;
    draw();
  };

  root.addEventListener("click", (event) => {
    const mode = event.target.closest("[data-mode]");
    if (mode) { view.mode = mode.dataset.mode; remember(); syncControls(); draw(); return; }
    const action = event.target.closest("[data-action]")?.dataset.action;
    if (action === "zoom-in") zoom(0.5);
    else if (action === "zoom-out") zoom(2);
    else if (action === "live") { view.live = !view.live; if (!view.live && painted) view.right = painted.t1; view.triggered = null; syncControls(); draw(); }
    else if (action === "single") { view.trigger.single = !view.trigger.single; view.live = true; view.triggered = null; syncControls(); draw(); }
    else if (action === "clear-cursors") { view.cursors = [null, null]; draw(); }
    else if (action === "export") download();
  });
  root.addEventListener("change", (event) => {
    const target = event.target;
    if (target.dataset.select === "trigger") { view.trigger.source = Number(target.value); view.triggered = null; view.live = true; syncControls(); }
    else if (target.dataset.select === "edge") { view.trigger.rising = target.value === "rising"; view.triggered = null; }
    else if (target.dataset.select === "uart") view.uart.source = Number(target.value);
    else if (target.dataset.select === "baud") view.uart.baud = Number(target.value);
    else if (target.dataset.channel === "auto") { view.channels = shown(); view.auto = target.checked; listed = ""; listPins(); }
    else if (target.dataset.channel !== undefined) {
      const index = Number(target.dataset.channel);
      const current = new Set(view.auto ? shown() : view.channels);
      if (target.checked) current.add(index); else current.delete(index);
      view.auto = false;
      view.channels = [...current].sort((a, b) => a - b);
      listed = "";
      listPins();
    }
    draw();
  });
  canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    zoom(event.deltaY < 0 ? 0.8 : 1.25, timeAt(event));
  }, {passive: false});
  let drag = null;
  canvas.addEventListener("pointerdown", (event) => {
    try { canvas.setPointerCapture(event.pointerId); } catch { /* Synthetic event */ }
    drag = {x: event.clientX, right: painted?.t1 ?? 0, moved: false, shift: event.shiftKey};
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!drag || !painted) return;
    if (!drag.moved && Math.abs(event.clientX - drag.x) < 4) return;
    drag.moved = true;
    hold();
    view.right = drag.right - ((event.clientX - drag.x) / painted.plot.w) * view.span;
    draw();
  });
  canvas.addEventListener("pointerup", (event) => {
    if (drag && !drag.moved) {
      const time = timeAt(event);
      if (time !== null) view.cursors[drag.shift || (view.cursors[0] !== null && view.cursors[1] === null && event.detail > 1) ? 1 : 0] = time;
      draw();
    }
    drag = null;
  });
  canvas.addEventListener("dblclick", () => { view.cursors = [null, null]; draw(); });

  function download() {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(new Blob([vcd()], {type: "text/plain"}));
    link.download = "siliconforge-capture.vcd";
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 10000);
    exported = performance.now();
    draw();
    setTimeout(draw, 6100);
  }
  let exported = -Infinity;

  function remember() {
    try { localStorage.setItem("siliconforge.instrument", view.mode); } catch { /* Storage may be unavailable */ }
  }
  try { view.mode = localStorage.getItem("siliconforge.instrument") === "scope" ? "scope" : "logic"; } catch { /* Storage may be unavailable */ }

  $("[data-select=baud]").replaceChildren(new Option("auto baud", "0"), ...bauds.map((rate) => new Option(`${rate} baud`, String(rate))));
  readColours();
  syncControls();
  new ResizeObserver(draw).observe(canvas);
  // The canvas takes its colours from the theme of the page.
  new MutationObserver(() => { readColours(); draw(); }).observe(document.documentElement, {attributes: true, attributeFilter: ["data-theme"]});

  return {
    /** Called on every state frame: fetches the new samples and redraws. */
    update() { listPins(); poll(); },
    // Automation API (window.siliconForge.instruments) --------------------------------------------------------------
    /** Resolves once the page holds everything recorded so far. */
    sync: () => poll().then(() => poll()),
    /** The recorded changes of a pin: {t: seconds[], v: volts[], s: states[]}. */
    samples: (name) => capture.channels.get(pinIndex(name)) ?? {t: [], v: [], s: []},
    measure, decodeUart, vcd, shown,
    /** The pins the instrument displays and the colour of their traces, in lane order: what the breadboard marks. */
    channels: () => shown().map((index, row) => ({pin: host.state?.pins[index]?.name ?? pinName(index), colour: palette[row % palette.length]})),
    get now() { return capture.now; },
    get view() { return view; },
    set(changes) { Object.assign(view, changes); listed = ""; listPins(); syncControls(); draw(); },
  };
};
