// One channel of a bench supply, as the breadboard shows it: a red marker with + on the positive hole, a black one with
// − on the negative hole, leads to a tag. The faceplate with the controls is in the Instruments panel (ui/devices.js).
"use strict";

SiliconForge.components.define("bench-supply", {
  create(ctx, component) {
    const plus = ctx.at.positive;
    const minus = ctx.at.negative;
    const offset = component.view.offset ?? [0, -44];
    const tag = {x: (plus.x + minus.x) / 2 + offset[0], y: (plus.y + minus.y) / 2 + offset[1]};
    ctx.layer.classList.add("bb-supply");
    ctx.probe(plus, tag, {kind: "plus", symbol: "+", title: `${component.id} +: positive output of the programmable supply`});
    ctx.probe(minus, tag, {kind: "minus", symbol: "−", title: `${component.id} −: negative output of the programmable supply`});
    const readout = ctx.instrumentTag(tag, component.id.toUpperCase()).reading;
    readout.textContent = "off";
    return {
      update(state) {
        readout.textContent = state.enabled ? `${(state.outputVoltage ?? 0).toFixed(2)} V ${state.mode === 2 ? "CC" : "CV"}` : "off";
      },
    };
  },
});
