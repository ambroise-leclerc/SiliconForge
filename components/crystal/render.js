// Quartz crystal (HC-49 can). Drawn only: the clock comes from the MCU description.
"use strict";

SiliconForge.components.define("crystal", {
  create(ctx, component) {
    const a = ctx.at.a;
    const b = ctx.at.b;
    ctx.layer.classList.add("bb-crystal");
    ctx.el("line", {x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: "bb-lead"}, ctx.layer);
    ctx.el("rect", {x: Math.min(a.x, b.x) - 3, y: a.y - 7, width: Math.abs(b.x - a.x) + 6, height: 14, rx: 7, class: "bb-crystal-body"}, ctx.layer);
    ctx.el("title", {}, ctx.layer, `${component.id}: ${component.properties.frequency / 1e6} MHz`);
    return {};
  },
});
