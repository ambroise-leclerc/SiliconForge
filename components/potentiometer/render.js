// Trimmer potentiometer: drag up or down, or use the wheel. The label shows the voltage of the wiper computed by the
// electrical model; while the user turns the knob it shows the expected value at once, then the model confirms it.
"use strict";

SiliconForge.components.define("potentiometer", {
  create(ctx, component) {
    const offset = component.view.offset ?? [0, -26];
    const points = [ctx.at.a, ctx.at.wiper, ctx.at.b];
    const centre = {x: points[1].x + offset[0], y: points[1].y + offset[1]};
    const supply = 5;
    ctx.layer.classList.add("bb-pot");
    for (const point of points)
      ctx.el("line", {x1: point.x, y1: point.y, x2: centre.x + (point.x - centre.x) * 0.6, y2: centre.y + 10, class: "bb-lead"}, ctx.layer);
    ctx.el("rect", {x: centre.x - 20, y: centre.y - 17, width: 40, height: 30, rx: 4, class: "bb-pot-body"}, ctx.layer);
    const knob = ctx.el("g", {}, ctx.layer);
    ctx.el("circle", {cx: centre.x, cy: centre.y - 2, r: 11, class: "bb-pot-knob"}, knob);
    ctx.el("line", {x1: centre.x, y1: centre.y - 2, x2: centre.x, y2: centre.y - 12, class: "bb-pot-mark"}, knob);
    ctx.el("rect", {x: centre.x + 24, y: centre.y - 9, width: 44, height: 16, rx: 3, class: "bb-value-box"}, ctx.layer);
    const value = ctx.el("text", {x: centre.x + 46, y: centre.y + 3, class: "bb-value", "text-anchor": "middle"}, ctx.layer, "0.00 V");
    ctx.el("title", {}, ctx.layer, `${component.id}: potentiometer${component.pin ? ` on ${component.pin}` : ""} — drag up or down, or use the wheel`);

    let position = 0;
    let settleUntil = 0;  // While the model catches up with the user, the pushed state is older than the knob.
    const show = (volts) => {
      knob.setAttribute("transform", `rotate(${-135 + position * 270} ${centre.x} ${centre.y - 2})`);
      value.textContent = `${volts.toFixed(2)} V`;
    };
    const turn = (next) => {
      position = Math.min(1, Math.max(0, next));
      settleUntil = performance.now() + 400;
      show(position * supply);
      ctx.set("position", position);
    };
    show(0);
    let drag = null;
    ctx.layer.addEventListener("pointerdown", (event) => { ctx.capture(ctx.layer, event); drag = {y: event.clientY, position}; });
    ctx.layer.addEventListener("pointermove", (event) => { if (drag) turn(drag.position + (drag.y - event.clientY) / 200); });
    ctx.layer.addEventListener("pointerup", () => (drag = null));
    ctx.layer.addEventListener("pointercancel", () => (drag = null));
    ctx.layer.addEventListener("wheel", (event) => {
      event.preventDefault();
      turn(position + (event.deltaY < 0 ? 0.02 : -0.02));
    }, {passive: false});
    return {
      update(state) {
        if (drag || performance.now() < settleUntil) return;
        position = state.position ?? 0;
        show(state.voltage ?? position * supply);
      },
    };
  },
});
