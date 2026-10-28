// Pinout view: a colour-coded diagram of the package, each pin with its port, functions and Arduino name, and its
// live level. Clicking a port label drives that input, as on the breadboard and in the pin list.
"use strict";

window.SiliconForgeViews = window.SiliconForgeViews || {};

window.SiliconForgeViews.pinout = (container, chip, ctx) => {
  const NS = "http://www.w3.org/2000/svg";
  const kinds = {
    ground: "GND", power: "Power", control: "Control", physical: "Physical pin", port: "Port pin", function: "Pin function",
    digital: "Digital pin", analog: "Analog related", pwm: "PWM pin", serial: "Serial pin", ide: "Arduino",
  };
  const slotWidths = [44, 66, 50, 40, 40, 50];
  const gap = 4;
  const rowHeight = 28;
  const half = chip.pins.length / 2;
  const sideWidth = slotWidths.reduce((sum, w) => sum + w + gap, 0) + 34;
  const chipWidth = 110;
  const width = 2 * sideWidth + chipWidth + 2 * 14;
  const top = 26;
  const height = top + half * rowHeight + 70;
  const chipLeft = 14 + sideWidth;
  const chipRight = chipLeft + chipWidth;

  const el = (tag, attributes = {}, parent = null, text = null) => {
    const element = document.createElementNS(NS, tag);
    for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
    if (text !== null) element.textContent = text;
    if (parent) parent.append(element);
    return element;
  };
  const box = (parent, x, y, w, text, kind) => {
    const group = el("g", {class: `po-box po-${kind}`}, parent);
    el("rect", {x, y: y - 10, width: w, height: 20, rx: 3}, group);
    el("text", {x: x + w / 2, y: y + 4, "text-anchor": "middle"}, group, text);
    return group;
  };

  const svg = el("svg", {viewBox: `0 0 ${width} ${height}`, class: "pinout", role: "img", "aria-label": `${chip.name} pinout`});
  const chipBottom = top + half * rowHeight - 6;
  el("rect", {x: chipLeft, y: top - 8, width: chipWidth, height: chipBottom - top + 8, rx: 6, class: "po-chip"}, svg);
  el("path", {d: `M ${(chipLeft + chipRight) / 2 - 12} ${top - 8} a 12 12 0 0 0 24 0`, class: "po-notch"}, svg);
  el("text", {x: (chipLeft + chipRight) / 2, y: (top + chipBottom) / 2, class: "po-chip-name", "text-anchor": "middle",
              transform: `rotate(90 ${(chipLeft + chipRight) / 2} ${(top + chipBottom) / 2})`}, svg, chip.name);

  const ports = new Map();  // Port name -> its box group
  for (const pin of chip.pins) {
    const leftSide = pin.n <= half;
    const row = leftSide ? pin.n - 1 : chip.pins.length - pin.n;
    const y = top + row * rowHeight + 6;
    const direction = leftSide ? -1 : 1;
    const edge = leftSide ? chipLeft : chipRight;
    el("rect", {x: leftSide ? edge - 8 : edge, y: y - 4, width: 8, height: 8, class: "po-leg"}, svg);
    el("text", {x: edge - direction * 14, y: y + 4, class: "po-number", "text-anchor": "middle"}, svg, pin.n);

    let x = edge + direction * 12;
    const lineEnd = {x, last: x};
    pin.labels.forEach((label, slot) => {
      const w = slotWidths[slot];
      const start = leftSide ? x - w : x;
      x += direction * (w + gap);
      if (!label) return;
      const [text, kind = chip.slotKinds[slot]] = label.split(":");
      const group = box(svg, start, y, w, text, kind);
      lineEnd.last = leftSide ? start : start + w;
      if (slot === 0 && kind === "port") {
        ports.set(text, group);
        group.classList.add("po-io");
        group.dataset.pin = text;
        el("circle", {cx: leftSide ? start + w - 7 : start + 7, cy: y, r: 3.5, class: "po-level"}, group);
        group.querySelector("text").setAttribute("x", start + w / 2 + (leftSide ? -4 : 4));
        el("title", {}, group, `Pin ${pin.n}`);
        group.addEventListener("click", () => ctx.cycle(text));
      }
    });
    // Thin line behind the labels, as on the reference diagrams
    svg.prepend(el("line", {x1: lineEnd.x, x2: lineEnd.last, y1: y, y2: y, class: "po-line"}));
  }

  const legend = el("g", {class: "po-legend"}, svg);
  let lx = 14;
  let ly = height - 46;
  for (const [kind, name] of Object.entries(kinds)) {
    const itemWidth = 30 + name.length * 6.4;
    if (lx + itemWidth > width - 14) {
      lx = 14;
      ly += 22;
    }
    el("rect", {x: lx, y: ly - 7, width: 14, height: 14, rx: 2}, el("g", {class: `po-box po-${kind}`}, legend));
    el("text", {x: lx + 19, y: ly + 4, class: "po-legend-text"}, legend, name);
    lx += itemWidth;
  }
  container.append(svg);

  return {
    update(state) {
      for (const pin of state.pins) {
        const group = ports.get(pin.name);
        if (!group) continue;
        group.dataset.state = pin.state;
        group.dataset.output = pin.output;
        group.dataset.forced = ctx.forced(pin.name);
        group.style.setProperty("--duty", pin.output ? pin.duty : pin.state === 1 ? 1 : 0);
        group.querySelector("title").textContent =
          `${pin.name}${pin.label ? ` (${pin.label})` : ""}: ${pin.output ? "output" : pin.pullUp ? "input, pull-up" : "input"}, ` +
          `${["low", "high", "floating"][pin.state]}${pin.output && pin.edges >= 2 ? `, PWM ${Math.round(pin.duty * 100)} %` : ""}` +
          (pin.output ? "" : " — click to drive");
      }
    },
  };
};
