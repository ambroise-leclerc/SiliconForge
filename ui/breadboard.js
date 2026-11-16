// Breadboard view: the chip on a breadboard, in the circuit served by sf.circuit() (ADR 0005).
//
// This file draws the board, the chip and its live pins. It knows no component type: each component of the circuit is
// drawn by the rendering module of its type, components/<type>/render.js, loaded on demand. A module registers itself
// with SiliconForge.components.define(type, {create(ctx, component)}); create() draws into ctx.layer and returns
// {update(state)}, called on every frame with the state of the component computed by the electrical model in C++
// (brightness, pressed, position...). User interactions go back through ctx.set(name, value), i.e. sf.component.set.
"use strict";

window.SiliconForge = window.SiliconForge || {};
window.SiliconForge.components = window.SiliconForge.components || (() => {
  const types = new Map();    // Type -> rendering module
  const loading = new Map();  // Type -> Promise of its script
  return {
    define(type, renderer) { types.set(type, renderer); },
    get: (type) => types.get(type),
    /** Loads components/<type>/render.js once. Resolves to the module, or to undefined when the type has none. */
    load(type) {
      if (types.has(type)) return Promise.resolve(types.get(type));
      if (!loading.has(type)) {
        loading.set(type, new Promise((resolve) => {
          const script = document.createElement("script");
          script.src = `components/${encodeURIComponent(type)}/render.js`;
          script.onload = () => resolve(types.get(type));
          script.onerror = () => resolve(undefined);
          document.head.append(script);
        }));
      }
      return loading.get(type);
    },
  };
})();

window.SiliconForgeViews = window.SiliconForgeViews || {};

window.SiliconForgeViews.breadboard = (container, chip, ctx, circuit) => {
  const NS = "http://www.w3.org/2000/svg";
  const pitch = 18;
  const left = 36;
  const half = chip.pins.length / 2;
  // Without a circuit the chip sits alone in the middle of a board long enough for it.
  const columns = circuit ? circuit.board.columns : Math.max(30, half + 10);
  const first = circuit ? circuit.chip.column : Math.floor((columns - half) / 2) + 1;
  const width = left * 2 + (columns - 1) * pitch;
  const height = 340;
  const rowY = {j: 78, i: 96, h: 114, g: 132, f: 150, e: 186, d: 204, c: 222, b: 240, a: 258};
  const railY = {"T-": 22, "T+": 40, "B+": 296, "B-": 314};
  const glow = `bb-glow-${Math.random().toString(36).slice(2, 8)}`;

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

  const svg = el("svg", {viewBox: `0 0 ${width} ${height}`, class: "breadboard", role: "img",
                         "aria-label": `${chip.name} on a breadboard${circuit ? `: ${circuit.name}` : ""}`});
  const defs = el("defs", {}, svg);
  el("feGaussianBlur", {stdDeviation: 5}, el("filter", {id: glow, x: "-100%", y: "-100%", width: "300%", height: "300%"}, defs));

  // Board, rails and holes ----------------------------------------------------------------------------------------
  el("rect", {x: 2, y: 2, width: width - 4, height: height - 4, rx: 10, class: "bb-board"}, svg);
  el("rect", {x: 8, y: 160, width: width - 16, height: 16, rx: 3, class: "bb-channel"}, svg);
  for (const [name, y] of Object.entries(railY)) {
    const positive = name.endsWith("+");
    const lineY = name.startsWith("T") ? (positive ? y + 9 : y - 9) : (positive ? y - 9 : y + 9);
    el("line", {x1: 16, x2: width - 16, y1: lineY, y2: lineY, class: positive ? "bb-rail-plus" : "bb-rail-minus"}, svg);
    el("text", {x: 12, y: y + 4, class: positive ? "bb-rail-label plus" : "bb-rail-label", "text-anchor": "middle"}, svg, positive ? "+" : "−");
    el("title", {}, svg.lastChild, positive ? "5 V" : "GND");
    for (let column = 1; column <= columns; ++column)
      if ((column - 1) % 6 !== 5) el("rect", {x: columnX(column) - 3, y: y - 3, width: 6, height: 6, rx: 1.2, class: "bb-hole"}, svg);
  }
  for (const [row, y] of Object.entries(rowY)) {
    el("text", {x: 18, y: y + 3.5, class: "bb-label", "text-anchor": "middle"}, svg, row);
    el("text", {x: width - 18, y: y + 3.5, class: "bb-label", "text-anchor": "middle"}, svg, row);
    for (let column = 1; column <= columns; ++column)
      el("rect", {x: columnX(column) - 3, y: y - 3, width: 6, height: 6, rx: 1.2, class: "bb-hole"}, svg);
  }
  for (let column = 1; column <= columns; column += column === 1 ? 4 : 5) {
    el("text", {x: columnX(column), y: rowY.j - 12, class: "bb-label", "text-anchor": "middle"}, svg, column);
    el("text", {x: columnX(column), y: rowY.a + 18, class: "bb-label", "text-anchor": "middle"}, svg, column);
  }

  // Chip -----------------------------------------------------------------------------------------------------------
  const pinCount = chip.pins.length;
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
    if (pin.port) {
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
  container.append(svg);

  // Components: each one drawn by the module of its type ---------------------------------------------------------
  /** Keeps receiving the pointer outside the element; synthetic events (UI tests) have no capturable pointer. */
  const capture = (element, event) => {
    try { element.setPointerCapture(event.pointerId); } catch { /* Not an active pointer */ }
  };
  const drawn = [];  // {id, update}
  let lastState = null;
  const place = async (component, parent) => {
    const renderer = await window.SiliconForge.components.load(component.type);
    if (!svg.isConnected) return;  // The view was replaced meanwhile.
    const layer = el("g", {"data-id": component.id, "data-type": component.type}, parent);
    if (component.pin) layer.dataset.pin = component.pin;
    if (!renderer) {
      el("title", {}, layer, `${component.id}: no rendering module for "${component.type}"`);
      return;
    }
    const at = Object.fromEntries(Object.entries(component.at).map(([terminal, spec]) => [terminal, hole(spec)]));
    const instance = renderer.create({
      el, layer, at, hole, pitch, glow, capture,
      set: (name, value) => ctx.setComponent(component.id, name, value),
    }, component) || {};
    if (instance.update) {
      drawn.push({id: component.id, update: instance.update});
      if (lastState?.components?.[component.id]) instance.update(lastState.components[component.id]);
    }
  };
  if (circuit) {
    const wires = el("g", {class: "bb-wires"}, svg);
    const parts = el("g", {class: "bb-parts"}, svg);
    circuit.wires.forEach((wire, index) => place({
      id: `wire-${index + 1}`, type: "wire", at: {from: wire.from, to: wire.to}, properties: {color: wire.color ?? "red"}, view: wire.view ?? {}, pin: "",
    }, wires));
    for (const component of circuit.components) place(component, parts);
    svg.append(chipLayer);  // Above the wires that end next to it
  }

  return {
    update(state) {
      lastState = state;
      for (const pin of state.pins) {
        const leg = legs.get(pin.name);
        if (!leg) continue;
        leg.dataset.state = pin.state;
        leg.dataset.output = pin.output;
        leg.dataset.forced = ctx.forced(pin.name);
        leg.querySelector("title").textContent = leg.querySelector("title").textContent.replace(/ \(.*\)$/, "") +
          ` (${pin.output ? "output" : pin.pullUp ? "input, pull-up" : "input"}, ${["low", "high", "floating"][pin.state]})`;
      }
      for (const part of drawn) {
        const componentState = state.components?.[part.id];
        if (componentState) part.update(componentState);
      }
    },
  };
};
