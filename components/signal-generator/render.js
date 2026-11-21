// One channel of a signal generator, as the breadboard shows it: a yellow V marker on the output hole, a ground marker on
// the common hole, leads to a tag with the voltage held at this instant. The faceplate with the controls is in the
// Instruments panel (ui/devices.js).
"use strict";

SiliconForge.components.define("signal-generator", {
  create(ctx, component) {
    const output = ctx.at.output;
    const common = ctx.at.common;
    const offset = component.view.offset ?? [0, -44];
    const tag = {x: (output.x + common.x) / 2 + offset[0], y: (output.y + common.y) / 2 + offset[1]};
    ctx.layer.classList.add("bb-generator");
    ctx.probe(output, tag, {kind: "volt", symbol: "V", title: `${component.id} output: signal generator`});
    ctx.probe(common, tag, {kind: "com", symbol: "ground", title: `${component.id} common: signal generator`});
    const readout = ctx.instrumentTag(tag, component.id.toUpperCase()).reading;
    readout.textContent = "off";
    const names = ["sine", "square", "triangle", "arbitrary"];
    return {
      update(state) {
        readout.textContent = !state.enabled ? "off" : !state.mode ? "open" : `${names[state.waveform ?? 0]} ${(state.outputVoltage ?? 0).toFixed(2)} V`;
      },
    };
  },
});
