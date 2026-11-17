// 5 mm LED. Its brightness comes from the electrical model: the current through it, averaged over the frame, so a
// PWM-driven LED is dimmed like the eye sees it.
"use strict";

SiliconForge.components.define("led", {
  create(ctx, component) {
    const colours = {red: "#ff3b30", green: "#34d058", yellow: "#ffd60a", blue: "#3d8bff"};
    const offset = component.view.offset ?? [0, -30];
    const a = ctx.at.anode;
    const k = ctx.at.cathode;
    const centre = {x: (a.x + k.x) / 2 + offset[0], y: (a.y + k.y) / 2 + offset[1]};
    const colour = colours[component.properties.color] ?? component.properties.color;
    ctx.layer.classList.add("bb-led");
    ctx.el("polyline", {points: `${a.x},${a.y} ${a.x},${a.y - 6} ${centre.x - 3},${centre.y + 7}`, class: "bb-lead"}, ctx.layer);
    ctx.el("polyline", {points: `${k.x},${k.y} ${k.x},${k.y - 4} ${centre.x + 3},${centre.y + 7}`, class: "bb-lead"}, ctx.layer);
    const halo = ctx.el("circle", {cx: centre.x, cy: centre.y, r: 16, fill: colour, filter: `url(#${ctx.glow})`, opacity: 0}, ctx.layer);
    ctx.el("path", {d: `M ${centre.x - 9} ${centre.y + 6} h 18`, class: "bb-led-rim", stroke: colour}, ctx.layer);
    const lens = ctx.el("circle", {cx: centre.x, cy: centre.y, r: 8.5, fill: colour, class: "bb-led-lens"}, ctx.layer);
    ctx.el("ellipse", {cx: centre.x - 3, cy: centre.y - 3, rx: 2.5, ry: 3.5, class: "bb-led-shine"}, ctx.layer);
    const title = ctx.el("title", {}, ctx.layer, component.id);
    return {
      update(state) {
        const level = state.brightness ?? 0;
        halo.setAttribute("opacity", (level * 0.95).toFixed(3));
        lens.setAttribute("fill-opacity", (0.35 + level * 0.65).toFixed(3));
        ctx.layer.dataset.level = level.toFixed(2);
        title.textContent = `${component.id}${component.pin ? ` on ${component.pin}` : ""}: ${((state.current ?? 0) * 1000).toFixed(1)} mA`;
      },
    };
  },
});
