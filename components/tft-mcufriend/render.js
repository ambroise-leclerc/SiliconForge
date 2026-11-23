// MCUFRIEND TFT (ILI9481): a row of header pins under a miniature of the panel, which shows what the controller received. The
// pixels come from sf.screen when the revision state of the panel changes.
"use strict";

SiliconForge.components.define("tft-mcufriend", {
  create(ctx, component) {
    const points = Object.values(ctx.at);
    const left = Math.min(...points.map((p) => p.x));
    const right = Math.max(...points.map((p) => p.x));
    const top = Math.min(...points.map((p) => p.y));
    const width = Math.min(Math.max(right - left + 16, 120), 170);
    ctx.layer.classList.add("bb-tft");
    const scale = width / 320;
    const height = 480 * scale;
    const frame = ctx.el("rect", {x: left - 8, y: top - height - 14, width, height: height + 8, rx: 3, fill: "#1b1b1b", stroke: "#555"}, ctx.layer);
    const image = ctx.el("image", {x: left - 8 + 2, y: top - height - 10, width: width - 4, height, preserveAspectRatio: "none"}, ctx.layer);
    for (const [name, point] of Object.entries(ctx.at)) {
      ctx.el("circle", {cx: point.x, cy: point.y, r: 4, class: "bb-probe-ring"}, ctx.layer);
      ctx.el("title", {}, ctx.layer, `${component.id} ${name.toUpperCase()}`);
    }
    const title = ctx.el("title", {}, frame, `${component.id}: MCUFRIEND TFT (ILI9481)`);
    const canvas = document.createElement("canvas");
    canvas.width = 320;
    canvas.height = 480;
    const context = canvas.getContext("2d");
    const draw = (shown) => {
      const bytes = Uint8Array.from(atob(shown.rgb565), (c) => c.charCodeAt(0));
      const pixels = context.createImageData(shown.width, shown.height);
      for (let i = 0; i < shown.width * shown.height; i++) {
        const word = (bytes[2 * i] << 8) | bytes[2 * i + 1];
        const r = (word >> 11) & 31, g = (word >> 5) & 63, b = word & 31;
        pixels.data.set([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255], 4 * i);
      }
      context.putImageData(pixels, 0, 0);
      image.setAttribute("href", canvas.toDataURL());
    };
    // The pixels are too large for the frames: the page fetches them when the revision of the panel changes.
    let shownRevision, fetching = false, wanted;
    const refresh = async () => {
      if (fetching) return;
      fetching = true;
      try {
        while (shownRevision !== wanted) {
          const target = wanted;
          const shown = await ctx.screen();
          if (shown) draw(shown);
          shownRevision = shown ? shown.revision : target;
        }
      } catch (error) {
        console.warn(error.message);
      } finally {
        fetching = false;
      }
    };
    return {
      update(state) {
        if (state.revision !== undefined && state.revision !== wanted) {
          wanted = state.revision;
          if (wanted !== shownRevision) refresh();
        }
        title.textContent = `${component.id}: ${state.displayOn ? "on" : "off"}, ${Math.round(state.pixelsWritten ?? 0)} pixels written`;
      },
    };
  },
});
