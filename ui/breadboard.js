// Breadboard view: the chip on a breadboard with a few components wired to it (LEDs, a push button, a
// potentiometer), all driven by the emulated pins. The circuit comes from the chip description (atmega328p.js).
"use strict";

window.SiliconForgeViews = window.SiliconForgeViews || {};

window.SiliconForgeViews.breadboard = (container, chip, ctx) => {
  const NS = "http://www.w3.org/2000/svg";
  const pitch = 18;
  const left = 36;
  const board = chip.breadboard;
  const width = left * 2 + (board.columns - 1) * pitch;
  const height = 340;
  const rowY = {j: 78, i: 96, h: 114, g: 132, f: 150, e: 186, d: 204, c: 222, b: 240, a: 258};
  const railY = {"T-": 22, "T+": 40, "B+": 296, "B-": 314};
  const wireColours = {red: "#d8312b", black: "#2b2b2b", yellow: "#e8c51c", green: "#2fa84f", blue: "#2f6fd8", orange: "#ec7a1c", white: "#e8e8e8"};
  const ledColours = {red: "#ff3b30", green: "#34d058", yellow: "#ffd60a", blue: "#3d8bff"};

  const el = (tag, attributes = {}, parent = null, text = null) => {
    const element = document.createElementNS(NS, tag);
    for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
    if (text !== null) element.textContent = text;
    if (parent) parent.append(element);
    return element;
  };
  const columnX = (column) => left + (column - 1) * pitch;
  /** "e6" -> hole coordinates, "T+12" -> rail hole coordinates. */
  const hole = (spec) => {
    const rail = /^([TB][+-])(\d+)$/.exec(spec);
    if (rail) return {x: columnX(Number(rail[2])), y: railY[rail[1]]};
    const match = /^([a-j])(\d+)$/.exec(spec);
    if (!match) throw new Error(`breadboard: bad hole "${spec}"`);
    return {x: columnX(Number(match[2])), y: rowY[match[1]]};
  };

  const svg = el("svg", {viewBox: `0 0 ${width} ${height}`, class: "breadboard", role: "img", "aria-label": `${chip.name} on a breadboard`});
  const defs = el("defs", {}, svg);
  const glow = el("filter", {id: "bb-glow", x: "-100%", y: "-100%", width: "300%", height: "300%"}, defs);
  el("feGaussianBlur", {stdDeviation: 5}, glow);

  // Board, rails and holes ----------------------------------------------------------------------------------------
  el("rect", {x: 2, y: 2, width: width - 4, height: height - 4, rx: 10, class: "bb-board"}, svg);
  el("rect", {x: 8, y: 160, width: width - 16, height: 16, rx: 3, class: "bb-channel"}, svg);
  for (const [name, y] of Object.entries(railY)) {
    const positive = name.endsWith("+");
    const lineY = name.startsWith("T") ? (positive ? y + 9 : y - 9) : (positive ? y - 9 : y + 9);
    el("line", {x1: 16, x2: width - 16, y1: lineY, y2: lineY, class: positive ? "bb-rail-plus" : "bb-rail-minus"}, svg);
    el("text", {x: 12, y: y + 4, class: positive ? "bb-rail-label plus" : "bb-rail-label", "text-anchor": "middle"}, svg, positive ? "+" : "−");
    el("title", {}, svg.lastChild, positive ? "5 V" : "GND");
    for (let column = 1; column <= board.columns; ++column)
      if ((column - 1) % 6 !== 5) el("rect", {x: columnX(column) - 3, y: y - 3, width: 6, height: 6, rx: 1.2, class: "bb-hole"}, svg);
  }
  for (const [row, y] of Object.entries(rowY)) {
    el("text", {x: 18, y: y + 3.5, class: "bb-label", "text-anchor": "middle"}, svg, row);
    el("text", {x: width - 18, y: y + 3.5, class: "bb-label", "text-anchor": "middle"}, svg, row);
    for (let column = 1; column <= board.columns; ++column)
      el("rect", {x: columnX(column) - 3, y: y - 3, width: 6, height: 6, rx: 1.2, class: "bb-hole"}, svg);
  }
  for (let column = 1; column <= board.columns; column += column === 1 ? 4 : 5) {
    el("text", {x: columnX(column), y: rowY.j - 12, class: "bb-label", "text-anchor": "middle"}, svg, column);
    el("text", {x: columnX(column), y: rowY.a + 18, class: "bb-label", "text-anchor": "middle"}, svg, column);
  }

  // Chip -----------------------------------------------------------------------------------------------------------
  const pinCount = chip.pins.length;
  const half = pinCount / 2;
  const first = board.chipColumn;
  const chipLayer = el("g", {class: "bb-chip"}, svg);
  const chipLeft = columnX(first) - 10;
  const chipRight = columnX(first + half - 1) + 10;
  const chipTop = rowY.f - 6;
  const chipBottom = rowY.e + 6;
  const legs = new Map();  // Port name -> leg element
  for (const pin of chip.pins) {
    const bottom = pin.n <= half;
    const column = bottom ? first + pin.n - 1 : first + pinCount - pin.n;
    const port = pin.labels[0].split(":")[0];
    const leg = el("rect", {x: columnX(column) - 4, y: bottom ? chipBottom - 2 : chipTop - 6, width: 8, height: 8, rx: 1.5, class: "bb-leg"}, chipLayer);
    const description = pin.labels.filter(Boolean).map((label) => label.split(":")[0]).join(" · ");
    el("title", {}, leg, `Pin ${pin.n}: ${description}`);
    if (/^P[A-Z]\d$/.test(port)) {
      legs.set(port, leg);
      leg.classList.add("bb-io");
      leg.dataset.pin = port;
      leg.addEventListener("click", () => ctx.cycle(port));
    }
  }
  el("rect", {x: chipLeft, y: chipTop, width: chipRight - chipLeft, height: chipBottom - chipTop, rx: 2, class: "bb-chip-body"}, chipLayer);
  el("path", {d: `M ${chipLeft} ${168 - 7} a 7 7 0 0 1 0 14`, class: "bb-chip-notch"}, chipLayer);
  el("circle", {cx: columnX(first), cy: chipBottom - 7, r: 2.2, class: "bb-chip-dot"}, chipLayer);
  el("text", {x: (chipLeft + chipRight) / 2, y: 172, class: "bb-chip-name", "text-anchor": "middle"}, chipLayer, chip.name.toLowerCase());

  // Components ----------------------------------------------------------------------------------------------------
  const updaters = [];
  /** Keeps receiving the pointer outside the element; synthetic events (UI tests) have no capturable pointer. */
  const capture = (element, event) => {
    try { element.setPointerCapture(event.pointerId); } catch { /* Not an active pointer */ }
  };
  const leads = (from, to, parent) => el("line", {x1: from.x, y1: from.y, x2: to.x, y2: to.y, class: "bb-lead"}, parent);

  const builders = {
    wire({from, to, color = "red", bend = 0.22}) {
      const a = hole(from);
      const b = hole(to);
      const dx = b.x - a.x;
      const dy = b.y - a.y;
      const length = Math.hypot(dx, dy);
      const curve = Math.min(40, length * bend);
      const cx = (a.x + b.x) / 2 - (dy / length) * curve;
      const cy = (a.y + b.y) / 2 + (dx / length) * curve - (Math.abs(dx) > Math.abs(dy) ? curve : 0);
      const group = el("g", {class: "bb-wire"}, svg);
      const d = `M ${a.x} ${a.y} Q ${cx} ${cy} ${b.x} ${b.y}`;
      el("path", {d, class: "bb-wire-outline"}, group);
      el("path", {d, stroke: wireColours[color] ?? color, class: "bb-wire-core"}, group);
      for (const end of [a, b]) el("circle", {cx: end.x, cy: end.y, r: 2.6, class: "bb-wire-end"}, group);
    },

    resistor({from, to, label}) {
      const a = hole(from);
      const b = hole(to);
      const group = el("g", {class: "bb-resistor"}, svg);
      leads(a, b, group);
      const angle = (Math.atan2(b.y - a.y, b.x - a.x) * 180) / Math.PI;
      const body = el("g", {transform: `translate(${(a.x + b.x) / 2} ${(a.y + b.y) / 2}) rotate(${angle})`}, group);
      el("rect", {x: -16, y: -5, width: 32, height: 10, rx: 4.5, class: "bb-resistor-body"}, body);
      ["#d8312b", "#d8312b", "#7a4a1f", "#c9a227"].forEach((colour, index) =>
        el("rect", {x: -10 + index * 5 + (index === 3 ? 4 : 0), y: -5, width: 2.6, height: 10, fill: colour}, body));
      if (label) el("title", {}, group, label);
    },

    crystal({from, to, label}) {
      const a = hole(from);
      const b = hole(to);
      const group = el("g", {class: "bb-crystal"}, svg);
      leads(a, b, group);
      el("rect", {x: a.x - 3, y: a.y - 7, width: b.x - a.x + 6, height: 14, rx: 7, class: "bb-crystal-body"}, group);
      if (label) el("title", {}, group, label);
    },

    led({anode, cathode, pin, color = "red", offset = [0, -30]}) {
      const a = hole(anode);
      const k = hole(cathode);
      const centre = {x: (a.x + k.x) / 2 + offset[0], y: (a.y + k.y) / 2 + offset[1]};
      const group = el("g", {class: "bb-led", "data-pin": pin}, svg);
      el("polyline", {points: `${a.x},${a.y} ${a.x},${a.y - 6} ${centre.x - 3},${centre.y + 7}`, class: "bb-lead"}, group);
      el("polyline", {points: `${k.x},${k.y} ${k.x},${k.y - 4} ${centre.x + 3},${centre.y + 7}`, class: "bb-lead"}, group);
      const colour = ledColours[color] ?? color;
      const halo = el("circle", {cx: centre.x, cy: centre.y, r: 16, fill: colour, filter: "url(#bb-glow)", opacity: 0}, group);
      el("path", {d: `M ${centre.x - 9} ${centre.y + 6} h 18`, class: "bb-led-rim", stroke: colour}, group);
      const lens = el("circle", {cx: centre.x, cy: centre.y, r: 8.5, fill: colour, class: "bb-led-lens"}, group);
      el("ellipse", {cx: centre.x - 3, cy: centre.y - 3, rx: 2.5, ry: 3.5, class: "bb-led-shine"}, group);
      el("title", {}, group, `LED on ${pin}`);
      updaters.push(() => {
        const state = ctx.pin(pin);
        const level = state && state.output ? state.duty : 0;
        halo.setAttribute("opacity", (level * 0.95).toFixed(3));
        lens.setAttribute("fill-opacity", (0.35 + level * 0.65).toFixed(3));
        group.dataset.level = level.toFixed(2);
      });
    },

    button({from, to, pin}) {
      const a = hole(from);
      const b = hole(to);
      const centre = {x: (a.x + b.x) / 2, y: (a.y + b.y) / 2};
      const group = el("g", {class: "bb-button", "data-pin": pin}, svg);
      leads(a, b, group);
      el("rect", {x: centre.x - 12, y: centre.y - 12, width: 24, height: 24, rx: 3, class: "bb-button-body"}, group);
      el("circle", {cx: centre.x, cy: centre.y, r: 7, class: "bb-button-cap"}, group);
      el("title", {}, group, `Push button on ${pin} (to ground)`);
      let pressed = false;
      const press = (down) => {
        if (pressed === down) return;
        pressed = down;
        group.classList.toggle("pressed", down);
        ctx.setPin(pin, down ? 0 : null);
      };
      group.addEventListener("pointerdown", (event) => {
        capture(group, event);
        press(true);
      });
      group.addEventListener("pointerup", () => press(false));
      group.addEventListener("pointercancel", () => press(false));
    },

    potentiometer({legs: legHoles, pin, offset = [0, -26]}) {
      const points = legHoles.map(hole);
      const centre = {x: points[1].x + offset[0], y: points[1].y + offset[1]};
      const group = el("g", {class: "bb-pot", "data-pin": pin}, svg);
      for (const point of points) leads(point, {x: centre.x + (point.x - centre.x) * 0.6, y: centre.y + 10}, group);
      el("rect", {x: centre.x - 20, y: centre.y - 17, width: 40, height: 30, rx: 4, class: "bb-pot-body"}, group);
      const knob = el("g", {}, group);
      el("circle", {cx: centre.x, cy: centre.y - 2, r: 11, class: "bb-pot-knob"}, knob);
      el("line", {x1: centre.x, y1: centre.y - 2, x2: centre.x, y2: centre.y - 12, class: "bb-pot-mark"}, knob);
      el("rect", {x: centre.x + 24, y: centre.y - 9, width: 44, height: 16, rx: 3, class: "bb-value-box"}, group);
      const value = el("text", {x: centre.x + 46, y: centre.y + 3, class: "bb-value", "text-anchor": "middle"}, group, "0.00 V");
      el("title", {}, group, `Potentiometer on ${pin}: drag up or down, or use the wheel`);
      let volts = 0;
      const set = (next) => {
        volts = Math.min(5, Math.max(0, next));
        knob.setAttribute("transform", `rotate(${-135 + volts * 54} ${centre.x} ${centre.y - 2})`);
        value.textContent = `${volts.toFixed(2)} V`;
        ctx.setVoltage(pin, volts);
      };
      knob.setAttribute("transform", `rotate(-135 ${centre.x} ${centre.y - 2})`);
      let drag = null;
      group.addEventListener("pointerdown", (event) => {
        capture(group, event);
        drag = {y: event.clientY, volts};
      });
      group.addEventListener("pointermove", (event) => {
        if (drag) set(drag.volts + (drag.y - event.clientY) / 40);
      });
      group.addEventListener("pointerup", () => (drag = null));
      group.addEventListener("wheel", (event) => {
        event.preventDefault();
        set(volts + (event.deltaY < 0 ? 0.1 : -0.1));
      }, {passive: false});
    },
  };

  for (const component of board.components) builders[component.type](component);
  svg.append(chipLayer);  // Above the wires that end next to it
  container.append(svg);

  return {
    update(state) {
      for (const pin of state.pins) {
        const leg = legs.get(pin.name);
        if (!leg) continue;
        leg.dataset.state = pin.state;
        leg.dataset.output = pin.output;
        leg.dataset.forced = ctx.forced(pin.name);
        leg.querySelector("title").textContent = leg.querySelector("title").textContent.replace(/ \(.*\)$/, "") +
          ` (${pin.output ? "output" : pin.pullUp ? "input, pull-up" : "input"}, ${["low", "high", "floating"][pin.state]})`;
      }
      for (const update of updaters) update();
    },
  };
};
