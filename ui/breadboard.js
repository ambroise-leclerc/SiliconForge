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
  // What the breadboard keeps from one drawing to the next (a moved component redraws it): the outlined component.
  const remembered = (window.SiliconForgeViews.breadboardMemory ??= {selected: null});
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
  const drawn = new Map();  // Id -> update(state) of its rendering module
  const placed = new Map();  // Id -> {component, renderer, layer, footprint}: what can be drawn again elsewhere
  let lastState = null;
  /** Draws a component with its terminals in `holes`, in place of its previous drawing when there is one. */
  const draw = (entry, holes) => {
    const {component, renderer} = entry;
    const layer = el("g", {"data-id": component.id, "data-type": component.type});
    if (component.pin) layer.dataset.pin = component.pin;
    if (entry.layer) entry.layer.replaceWith(layer); else entry.parent.append(layer);
    entry.layer = layer;
    if (!renderer) {
      el("title", {}, layer, `${component.id}: no rendering module for "${component.type}"`);
      return;
    }
    const at = Object.fromEntries(Object.entries(holes).map(([terminal, spec]) => [terminal, hole(spec)]));
    const instance = renderer.create({
      el, layer, at, hole, pitch, glow, capture,
      set: (name, value) => ctx.setComponent(component.id, name, value),
    }, {...component, at: holes}) || {};
    if (isDipole(entry)) layer.classList.add("bb-movable");
    if (instance.update) {
      drawn.set(component.id, instance.update);
      if (lastState?.components?.[component.id]) instance.update(lastState.components[component.id]);
    }
  };
  const place = async (component, parent) => {
    const renderer = await window.SiliconForge.components.load(component.type);
    if (!svg.isConnected) return;  // The view was replaced meanwhile.
    const entry = {component, renderer, parent, layer: null, footprint: circuit?.types?.[component.type]?.footprint};
    placed.set(component.id, entry);
    draw(entry, component.at);
    if (remembered.selected === component.id) select(component.id);
  };

  // Moving the dipoles: a click outlines one and shows a square on each end; dragging the outlined body moves the
  // whole component from hole to hole, dragging a square moves that end only. The footprint of the type sets the
  // limits (same row and a span for a resistor, a maximum length for a wire), the C++ model checks them again.
  const rows = ["T-", "T+", "j", "i", "h", "g", "f", "e", "d", "c", "b", "a", "B+", "B-"];
  const rowPitch = {"T-": 0, "T+": 1, j: 3, i: 4, h: 5, g: 6, f: 7, e: 9, d: 10, c: 11, b: 12, a: 13, "B+": 15, "B-": 16};
  const rowOf = {...rowY, ...railY};
  const parse = (spec) => {
    const match = /^([TB][+-]|[a-j])(\d+)$/.exec(spec);
    return {row: match[1], column: Number(match[2])};
  };
  const format = ({row, column}) => `${row}${column}`;
  const isRail = (row) => row.length === 2;
  /** Holes drawn on the board: the rails have a gap every six columns. */
  const exists = ({row, column}) => column >= 1 && column <= columns && (!isRail(row) || (column - 1) % 6 !== 5);
  const length = (a, b) => Math.hypot(a.column - b.column, rowPitch[a.row] - rowPitch[b.row]);
  const taken = new Set();  // Holes of the leads and of the chip legs
  if (circuit) {
    for (const component of circuit.components) for (const spec of Object.values(component.at)) taken.add(spec);
    for (const wire of circuit.wires) { taken.add(wire.from); taken.add(wire.to); }
    for (let column = first; column < first + half; ++column) { taken.add(`e${column}`); taken.add(`f${column}`); }
  }
  /** Whether the footprint of `entry` accepts its terminals in `holes` (terminal -> spec). */
  const accepts = (entry, holes) => {
    const own = new Set(Object.values(entry.component.at));
    const specs = Object.values(holes);
    if (new Set(specs).size !== specs.length) return false;
    for (const spec of specs) {
      const where = parse(spec);
      if (!exists(where) && !own.has(spec)) return false;
      if (taken.has(spec) && !own.has(spec)) return false;
    }
    const [a, b] = specs.map(parse);
    const footprint = entry.footprint ?? {kind: "free"};
    if (footprint.kind === "free") return footprint.maximum === undefined || length(a, b) <= footprint.maximum + 1e-9;
    if (isRail(a.row) || isRail(b.row)) return false;
    if (footprint.kind === "span") {
      const span = Math.abs(a.column - b.column);
      return a.row === b.row && span >= footprint.minimum && span <= footprint.maximum;
    }
    const [first, second] = Object.keys(holes);  // Fixed: the offsets, as described or mirrored along the row
    const dx = footprint.offsets[second][0] - footprint.offsets[first][0];
    const dy = footprint.offsets[second][1] - footprint.offsets[first][1];
    const placedA = parse(holes[first]);
    const placedB = parse(holes[second]);
    const columnStep = placedB.column - placedA.column;
    return rows.indexOf(placedB.row) - rows.indexOf(placedA.row) === dy && (columnStep === dx || columnStep === -dx);
  };
  const isDipole = (entry) => entry.renderer && entry.footprint && Object.keys(entry.component.at).length === 2;
  const describe = (entry, holes) => {
    const [a, b] = Object.values(holes).map(parse);
    const name = circuit.types?.[entry.component.type]?.name ?? entry.component.type;
    const footprint = entry.footprint;
    if (footprint.kind === "span") {
      const span = Math.abs(a.column - b.column);
      const range = footprint.minimum === footprint.maximum ? `${footprint.maximum}` : `${footprint.minimum}–${footprint.maximum}`;
      return `${name} · ${span} hole${span === 1 ? "" : "s"} (${range})`;
    }
    if (footprint.kind === "free" && footprint.maximum !== undefined) return `${name} · ${length(a, b).toFixed(1)} of ${footprint.maximum} holes`;
    return name;
  };

  const overlay = el("g", {class: "bb-selection"});
  let selected = null;  // {entry, holes}: the outlined component and where it is drawn now
  const showSelection = () => {
    overlay.replaceChildren();
    if (!selected) return;
    const {entry, holes} = selected;
    entry.layer.classList.add("bb-selected");
    for (const [terminal, spec] of Object.entries(holes)) {
      const {x, y} = hole(spec);
      const handle = el("rect", {x: x - 4.5, y: y - 4.5, width: 9, height: 9, rx: 1.5, class: "bb-handle", "data-terminal": terminal}, overlay);
      el("title", {}, handle, `${entry.component.id} ${terminal}: ${spec}. Drag to move this end only.`);
    }
    let box = null;
    try { box = entry.layer.getBBox(); } catch { /* Not rendered */ }
    if (!box) return;
    const above = box.y > 22;
    el("text", {x: box.x + box.width / 2, y: above ? box.y - 7 : box.y + box.height + 15, class: "bb-limits", "text-anchor": "middle"},
       overlay, describe(entry, holes));
  };
  const select = (id) => {
    const entry = placed.get(id);
    if (selected) selected.entry.layer.classList.remove("bb-selected");
    selected = entry && isDipole(entry) ? {entry, holes: {...entry.component.at}} : null;
    remembered.selected = selected ? id : null;
    showSelection();
  };
  const moveTo = (holes) => {
    if (!selected || Object.entries(holes).every(([terminal, spec]) => selected.holes[terminal] === spec)) return;
    selected.holes = holes;
    draw(selected.entry, holes);
    showSelection();
  };
  /** Sends the new holes to the model; the board is redrawn from the new circuit, or put back if refused. */
  const commit = () => {
    const {entry, holes} = selected;
    const changed = Object.fromEntries(Object.entries(holes).filter(([terminal, spec]) => entry.component.at[terminal] !== spec));
    if (!Object.keys(changed).length) return;
    Promise.resolve(ctx.moveComponent?.(entry.component.id, changed)).catch(() => {
      if (!svg.isConnected || selected?.entry !== entry) return;
      moveTo({...entry.component.at});
    });
  };
  /** The valid placement whose `terminal` lands nearest to `point`, among the candidates. */
  const nearest = (candidates, terminal, point) => {
    let best = null;
    let bestDistance = Infinity;
    for (const holes of candidates) {
      if (!accepts(selected.entry, holes)) continue;
      const {x, y} = hole(holes[terminal]);
      const distance = Math.hypot(x - point.x, y - point.y) - (holes === candidates.current ? 0.01 : 0);
      if (distance < bestDistance) { best = holes; bestDistance = distance; }
    }
    return best;
  };
  /** Every placement of the whole component, translated by whole holes; or of one end, the other staying. */
  function* translations(origin) {
    const terminals = Object.entries(origin).map(([terminal, spec]) => [terminal, parse(spec)]);
    for (let dr = -rows.length; dr <= rows.length; ++dr)
      for (let dc = -columns; dc <= columns; ++dc) {
        const holes = {};
        let inside = true;
        for (const [terminal, where] of terminals) {
          const row = rows[rows.indexOf(where.row) + dr];
          if (!row || where.column + dc < 1 || where.column + dc > columns) { inside = false; break; }
          holes[terminal] = format({row, column: where.column + dc});
        }
        if (inside) yield holes;
      }
  }
  function* ends(origin, terminal) {
    for (const row of rows)
      for (let column = 1; column <= columns; ++column) yield {...origin, [terminal]: format({row, column})};
  }
  const pointIn = (event) => {
    const matrix = svg.getScreenCTM();
    if (!matrix) return null;
    return new DOMPoint(event.clientX, event.clientY).matrixTransform(matrix.inverse());
  };

  let gesture = null;  // {kind: "body" | "end", id, terminal, start, offset, dragging, pointerId}
  svg.addEventListener("pointerdown", (event) => {
    if (event.button !== 0 || !circuit) return;
    const handle = event.target.closest?.(".bb-handle");
    if (handle && selected) {
      event.stopPropagation();
      event.preventDefault();
      capture(svg, event);
      gesture = {kind: "end", terminal: handle.dataset.terminal, origin: {...selected.holes}, dragging: true, pointerId: event.pointerId};
      svg.classList.add("bb-dragging");
      return;
    }
    const layer = event.target.closest?.("[data-id]");
    const entry = layer && placed.get(layer.dataset.id);
    if (!entry || !isDipole(entry)) {
      if (!event.target.closest?.(".bb-selection")) select(null);
      return;
    }
    const point = pointIn(event);
    gesture = {kind: "body", id: entry.component.id, screen: {x: event.clientX, y: event.clientY}, point, dragging: false, pointerId: event.pointerId};
  }, true);
  svg.addEventListener("pointermove", (event) => {
    if (!gesture || event.pointerId !== gesture.pointerId) return;
    const point = pointIn(event);
    if (!point) return;
    if (gesture.kind === "body" && !gesture.dragging) {
      if (selected?.entry.component.id !== gesture.id || !gesture.point) return;
      if (Math.hypot(event.clientX - gesture.screen.x, event.clientY - gesture.screen.y) < 4) return;
      // The drag starts: the component lets go of the pointer (a push button is released).
      gesture.dragging = true;
      gesture.origin = {...selected.holes};
      const grabbed = Object.entries(gesture.origin).map(([terminal, spec]) => [terminal, hole(spec)])
        .sort(([, a], [, b]) => Math.hypot(a.x - gesture.point.x, a.y - gesture.point.y) - Math.hypot(b.x - gesture.point.x, b.y - gesture.point.y))[0];
      gesture.terminal = grabbed[0];
      gesture.offset = {x: grabbed[1].x - gesture.point.x, y: grabbed[1].y - gesture.point.y};
      selected.entry.layer.dispatchEvent(new PointerEvent("pointercancel", {pointerId: event.pointerId}));
      capture(svg, event);
      svg.classList.add("bb-dragging");
    }
    if (!gesture.dragging) return;
    const target = gesture.kind === "body" ? {x: point.x + gesture.offset.x, y: point.y + gesture.offset.y} : point;
    const candidates = gesture.kind === "body" ? [...translations(gesture.origin)] : [...ends(gesture.origin, gesture.terminal)];
    candidates.current = candidates.find((holes) => Object.entries(holes).every(([t, spec]) => selected.holes[t] === spec));
    const best = nearest(candidates, gesture.terminal, target);
    if (best) moveTo(best);
  });
  const finish = (event, cancelled) => {
    if (!gesture || event.pointerId !== gesture.pointerId) return;
    const done = gesture;
    gesture = null;
    svg.classList.remove("bb-dragging");
    if (!done.dragging) {
      if (done.kind === "body" && !cancelled) select(done.id);
      return;
    }
    if (cancelled) moveTo(done.origin);
    else commit();
  };
  svg.addEventListener("pointerup", (event) => finish(event, false));
  svg.addEventListener("pointercancel", (event) => finish(event, true));
  // Keyboard: Escape lets go of the component, the arrows move it by one hole.
  const onKey = (event) => {
    if (!svg.isConnected) { document.removeEventListener("keydown", onKey); return; }
    if (!selected || gesture || event.target.closest?.("input, textarea, select")) return;
    if (event.key === "Escape") { select(null); return; }
    const step = {ArrowLeft: [0, -1], ArrowRight: [0, 1], ArrowUp: [-1, 0], ArrowDown: [1, 0]}[event.key];
    if (!step) return;
    event.preventDefault();
    const holes = {};
    for (const [terminal, spec] of Object.entries(selected.holes)) {
      const where = parse(spec);
      const row = rows[rows.indexOf(where.row) + step[0]];
      if (!row || where.column + step[1] < 1 || where.column + step[1] > columns) return;
      holes[terminal] = format({row, column: where.column + step[1]});
    }
    if (!accepts(selected.entry, holes)) return;
    moveTo(holes);
    commit();
  };
  document.addEventListener("keydown", onKey);

  if (circuit) {
    const wires = el("g", {class: "bb-wires"}, svg);
    const parts = el("g", {class: "bb-parts"}, svg);
    circuit.wires.forEach((wire, index) => place({
      id: `wire-${index + 1}`, type: "wire", at: {from: wire.from, to: wire.to}, properties: {color: wire.color ?? "red"}, view: wire.view ?? {}, pin: "",
    }, wires));
    for (const component of circuit.components) place(component, parts);
    svg.append(chipLayer);  // Above the wires that end next to it
    svg.append(overlay);    // The handles above everything
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
      for (const [id, update] of drawn) {
        const componentState = state.components?.[id];
        if (componentState) update(componentState);
      }
    },
  };
};
