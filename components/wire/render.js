// Jumper wire: a curved line between two holes, in the colour of its "color" property.
"use strict";

SiliconForge.components.define("wire", {
  create(ctx, component) {
    const colours = {red: "#d8312b", black: "#2b2b2b", yellow: "#e8c51c", green: "#2fa84f", blue: "#2f6fd8", orange: "#ec7a1c", white: "#e8e8e8"};
    const a = ctx.at.from;
    const b = ctx.at.to;
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const length = Math.hypot(dx, dy) || 1;
    const curve = Math.min(40, length * (component.view.bend ?? 0.22));
    const cx = (a.x + b.x) / 2 - (dy / length) * curve;
    const cy = (a.y + b.y) / 2 + (dx / length) * curve - (Math.abs(dx) > Math.abs(dy) ? curve : 0);
    const d = `M ${a.x} ${a.y} Q ${cx} ${cy} ${b.x} ${b.y}`;
    ctx.layer.classList.add("bb-wire");
    ctx.el("path", {d, class: "bb-wire-outline"}, ctx.layer);
    ctx.el("path", {d, stroke: colours[component.properties.color] ?? component.properties.color, class: "bb-wire-core"}, ctx.layer);
    for (const end of [a, b]) ctx.el("circle", {cx: end.x, cy: end.y, r: 2.6, class: "bb-wire-end"}, ctx.layer);
    return {};
  },
});
