// Tactile push button: pressed while the pointer (or the space bar, when focused) holds it.
"use strict";

SiliconForge.components.define("push-button", {
  create(ctx, component) {
    const a = ctx.at.a;
    const b = ctx.at.b;
    const centre = {x: (a.x + b.x) / 2, y: (a.y + b.y) / 2};
    ctx.layer.classList.add("bb-button");
    ctx.layer.setAttribute("tabindex", "0");
    ctx.layer.setAttribute("role", "button");
    ctx.layer.setAttribute("aria-label", `Push button ${component.id}`);
    ctx.el("line", {x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: "bb-lead"}, ctx.layer);
    ctx.el("rect", {x: centre.x - 12, y: centre.y - 12, width: 24, height: 24, rx: 3, class: "bb-button-body"}, ctx.layer);
    ctx.el("circle", {cx: centre.x, cy: centre.y, r: 7, class: "bb-button-cap"}, ctx.layer);
    ctx.el("title", {}, ctx.layer, `${component.id}: push button${component.pin ? ` on ${component.pin}` : ""}`);
    let pressed = false;
    const press = (down) => {
      if (pressed === down) return;
      pressed = down;
      ctx.layer.classList.toggle("pressed", down);
      ctx.set("pressed", down ? 1 : 0);
    };
    ctx.layer.addEventListener("pointerdown", (event) => { ctx.capture(ctx.layer, event); press(true); });
    ctx.layer.addEventListener("pointerup", () => press(false));
    ctx.layer.addEventListener("pointercancel", () => press(false));
    ctx.layer.addEventListener("keydown", (event) => { if (event.key === " " || event.key === "Enter") { event.preventDefault(); event.stopPropagation(); press(true); } });
    ctx.layer.addEventListener("keyup", (event) => { if (event.key === " " || event.key === "Enter") { event.stopPropagation(); press(false); } });
    ctx.layer.addEventListener("blur", () => press(false));
    return {
      update(state) {
        // Another window, or a test bench, may hold the button.
        if (!pressed) ctx.layer.classList.toggle("pressed", Boolean(state.pressed));
      },
    };
  },
});
