// One-digit 7-segment display. Each segment is an LED of the electrical model: its brightness is the current through
// it. `data-digit` carries the character the lit segments form (empty when they form none), for tests and screen readers.
"use strict";

SiliconForge.components.define("seven-segment", {
  create(ctx, component) {
    const colours = {red: "#ff3b30", green: "#34d058", yellow: "#ffd60a", blue: "#3d8bff"};
    const colour = colours[component.properties.color] ?? component.properties.color;
    const left = Math.min(ctx.at.g.x, ctx.at.b.x) - 7;
    const right = Math.max(ctx.at.g.x, ctx.at.b.x) + 7;
    const top = ctx.at.g.y - 46;
    const bottom = ctx.at.e.y + 46;
    ctx.layer.classList.add("bb-seven-segment");
    ctx.layer.setAttribute("role", "img");
    ctx.el("rect", {x: left, y: top, width: right - left, height: bottom - top, rx: 4, class: "bb-seg-body"}, ctx.layer);
    // The digit, slanted like the real part, in a box of 44 x 84 centred in the body.
    const cx = (left + right) / 2 - 3;
    const cy = (top + bottom) / 2;
    const w = 20;   // Half width of the digit
    const h = 34;   // Half height
    const t = 4.2;  // Half thickness of a segment
    const horizontal = (y) => `M ${-w + t + 1.5} ${y} l ${t} ${-t} h ${2 * (w - 2 * t - 1.5)} l ${t} ${t} l ${-t} ${t} h ${-2 * (w - 2 * t - 1.5)} Z`;
    const vertical = (x, y0, y1) => `M ${x} ${y0 + t + 1.5} l ${t} ${t} v ${y1 - y0 - 4 * t - 3} l ${-t} ${t} l ${-t} ${-t} v ${-(y1 - y0 - 4 * t - 3)} Z`;
    const shapes = {
      a: horizontal(-h), g: horizontal(0), d: horizontal(h),
      f: vertical(-w, -h, 0), b: vertical(w, -h, 0), e: vertical(-w, 0, h), c: vertical(w, 0, h),
    };
    const digit = ctx.el("g", {transform: `translate(${cx} ${cy}) skewX(-6)`}, ctx.layer);
    const segments = {};
    for (const [name, d] of Object.entries(shapes)) {
      ctx.el("path", {d, class: "bb-seg-off"}, digit);
      segments[name] = ctx.el("path", {d, fill: colour, "fill-opacity": 0, class: "bb-seg-on", "data-segment": name}, digit);
    }
    ctx.el("circle", {cx: w + 9, cy: h, r: 3.6, class: "bb-seg-off"}, digit);
    segments.dp = ctx.el("circle", {cx: w + 9, cy: h, r: 3.6, fill: colour, "fill-opacity": 0, class: "bb-seg-on", "data-segment": "dp"}, digit);
    const title = ctx.el("title", {}, ctx.layer, component.id);

    const glyphs = {
      abcdef: "0", bc: "1", abdeg: "2", abcdg: "3", bcfg: "4", acdfg: "5", acdefg: "6", abc: "7", abcdefg: "8", abcdfg: "9",
      abcefg: "A", cdefg: "b", adef: "C", bcdeg: "d", adefg: "E", aefg: "F", g: "-", "": " ",
    };
    return {
      update(state) {
        let lit = "";
        for (const [name, element] of Object.entries(segments)) {
          const level = state[`${name}.brightness`] ?? 0;
          element.setAttribute("fill-opacity", level.toFixed(3));
          element.style.filter = level > 0.05 ? `drop-shadow(0 0 ${(3 * level).toFixed(1)}px ${colour})` : "";
          if (name !== "dp" && level > 0.5) lit += name;
        }
        const shown = glyphs[[...lit].sort().join("")] ?? "";
        ctx.layer.dataset.digit = shown.trim();
        ctx.layer.dataset.point = (state["dp.brightness"] ?? 0) > 0.5;
        ctx.layer.setAttribute("aria-label", `7-segment display ${component.id}: ${shown.trim() || "blank"}`);
        title.textContent = `${component.id}: ${shown.trim() || "blank"}`;
      },
    };
  },
});
