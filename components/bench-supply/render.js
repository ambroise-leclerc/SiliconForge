// One channel of a bench supply: a small instrument box with the voltage and the current it delivers, CV or CC.
"use strict";

SiliconForge.components.define("bench-supply", {
  create(ctx, component) {
    const plus = ctx.at.positive;
    const minus = ctx.at.negative;
    const centre = {x: (plus.x + minus.x) / 2, y: Math.min(plus.y, minus.y) - 34};
    ctx.layer.classList.add("bb-supply");
    ctx.el("line", {x1: plus.x, y1: plus.y, x2: plus.x, y2: centre.y + 14, class: "bb-lead", stroke: "#d8312b"}, ctx.layer);
    ctx.el("line", {x1: minus.x, y1: minus.y, x2: minus.x, y2: centre.y + 14, class: "bb-lead", stroke: "#1b1b1b"}, ctx.layer);
    ctx.el("rect", {x: centre.x - 38, y: centre.y - 16, width: 76, height: 30, rx: 4, class: "bb-pot-body"}, ctx.layer);
    const volts = ctx.el("text", {x: centre.x, y: centre.y - 3, class: "bb-value", "text-anchor": "middle"}, ctx.layer, "off");
    const amps = ctx.el("text", {x: centre.x, y: centre.y + 9, class: "bb-value", "text-anchor": "middle"}, ctx.layer, "");
    ctx.el("title", {}, ctx.layer, `${component.id}: programmable supply`);
    return {
      update(state) {
        if (!state.enabled) {
          volts.textContent = "off";
          amps.textContent = "";
          return;
        }
        volts.textContent = `${(state.outputVoltage ?? 0).toFixed(2)} V ${state.mode === 2 ? "CC" : "CV"}`;
        amps.textContent = `${((state.outputCurrent ?? 0) * 1000).toFixed(1)} mA`;
      },
    };
  },
});
