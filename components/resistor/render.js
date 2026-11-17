// Axial resistor with its four colour bands computed from the "resistance" property (5 % tolerance).
"use strict";

SiliconForge.components.define("resistor", {
  create(ctx, component) {
    const bands = ["#1b1b1b", "#7a4a1f", "#d8312b", "#ec7a1c", "#e8c51c", "#2fa84f", "#2f6fd8", "#8b4fc9", "#8a8a8a", "#f2f2f2"];
    const ohms = component.properties.resistance;
    // Two significant digits and a power of ten: 220 -> red red brown, 4700 -> yellow violet red.
    let exponent = Math.floor(Math.log10(ohms)) - 1;
    let digits = Math.round(ohms / 10 ** exponent);
    if (digits >= 100) { digits = 10; exponent += 1; }
    const multiplier = exponent === -1 ? "#c9a227" : exponent === -2 ? "#c0c0c0" : bands[Math.min(9, Math.max(0, exponent))];
    const a = ctx.at.a;
    const b = ctx.at.b;
    ctx.layer.classList.add("bb-resistor");
    ctx.el("line", {x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: "bb-lead"}, ctx.layer);
    const angle = (Math.atan2(b.y - a.y, b.x - a.x) * 180) / Math.PI;
    const body = ctx.el("g", {transform: `translate(${(a.x + b.x) / 2} ${(a.y + b.y) / 2}) rotate(${angle})`}, ctx.layer);
    ctx.el("rect", {x: -16, y: -5, width: 32, height: 10, rx: 4.5, class: "bb-resistor-body"}, body);
    [bands[Math.floor(digits / 10)], bands[digits % 10], multiplier, "#c9a227"].forEach((colour, index) =>
      ctx.el("rect", {x: -10 + index * 5 + (index === 3 ? 4 : 0), y: -5, width: 2.6, height: 10, fill: colour}, body));
    const label = ohms >= 1e6 ? `${ohms / 1e6} MΩ` : ohms >= 1e3 ? `${ohms / 1e3} kΩ` : `${ohms} Ω`;
    ctx.el("title", {}, ctx.layer, `${component.id}: ${label}`);
    return {};
  },
});
