// Variable resistive load: a resistor body whose label shows the resistance and the current drawn.
"use strict";

SiliconForge.components.define("variable-load", {
  create(ctx, component) {
    const a = ctx.at.a;
    const b = ctx.at.b;
    ctx.layer.classList.add("bb-resistor");
    ctx.el("line", {x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: "bb-lead"}, ctx.layer);
    const angle = (Math.atan2(b.y - a.y, b.x - a.x) * 180) / Math.PI;
    const body = ctx.el("g", {transform: `translate(${(a.x + b.x) / 2} ${(a.y + b.y) / 2}) rotate(${angle})`}, ctx.layer);
    ctx.el("rect", {x: -16, y: -6, width: 32, height: 12, rx: 3, class: "bb-resistor-body"}, body);
    ctx.el("rect", {x: -2, y: -6, width: 4, height: 12, fill: "#8a8a8a"}, body);
    const title = ctx.el("title", {}, ctx.layer, `${component.id}: variable load`);
    const format = (ohms) => (ohms >= 1e6 ? `${ohms / 1e6} MΩ` : ohms >= 1e3 ? `${ohms / 1e3} kΩ` : `${ohms} Ω`);
    return {
      update(state) {
        const current = state.current ?? 0;
        title.textContent = `${component.id}: ${format(state.resistance ?? 0)}, ${(current * 1000).toFixed(1)} mA`;
      },
    };
  },
});
