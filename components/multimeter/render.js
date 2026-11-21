// A multimeter: a yellow V marker and a COM marker with the ground symbol, leads to a tag that shows the reading.
"use strict";

SiliconForge.components.define("multimeter", {
  create(ctx, component) {
    const v = ctx.at.v;
    const com = ctx.at.com;
    const offset = component.view.offset ?? [0, -44];
    const tag = {x: (v.x + com.x) / 2 + offset[0], y: (v.y + com.y) / 2 + offset[1]};
    ctx.layer.classList.add("bb-multimeter");
    ctx.probe(v, tag, {kind: "volt", symbol: "V", title: `${component.id} V: voltage input of the multimeter`});
    ctx.probe(com, tag, {kind: "com", symbol: "ground", title: `${component.id} COM: common of the multimeter`});
    const readout = ctx.instrumentTag(tag, component.id.toUpperCase()).reading;
    readout.textContent = "— V";
    return {
      update(state) { readout.textContent = `${(state.voltage ?? 0).toFixed(3)} V`; },
    };
  },
});
