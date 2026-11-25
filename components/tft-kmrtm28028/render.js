// KMRTM28028 TFT (ILI9341): a row of header pins under a miniature of the panel, which shows what the controller received. The
// pixels come from sf.screen when the revision state of the panel changes.
"use strict";

SiliconForge.components.define("tft-kmrtm28028", {
  create(ctx, component) {
    const points = Object.values(ctx.at);
    const left = Math.min(...points.map((p) => p.x));
    const right = Math.max(...points.map((p) => p.x));
    const top = Math.min(...points.map((p) => p.y));
    ctx.layer.classList.add("bb-tft");
    // The panel is a floating plate: the user drags it off the board to keep the board at full size (ctx.floating).
    // It is shown in landscape unless the firmware chose the landscape orientation itself, and its connectors, along
    // the bottom edge, take the colour of the wire plugged in the same strip of holes on the board.
    const bezel = 3;
    const longest = 170;
    const strip = 36;
    const names = Object.keys(ctx.at);
    // Above the chip and the probes (ctx.plate), so that a plate dragged over them stays in front.
    const host = ctx.plate?.(component.id) ?? ctx.layer;
    const leads = ctx.el("g", {class: "bb-tft-leads", "pointer-events": "none"}, host);
    const panel = ctx.el("g", {class: "bb-tft-panel"}, host);
    const frame = ctx.el("rect", {rx: 2, fill: "#111", stroke: "#444"}, panel);
    // Two images in turn: a new picture is shown once it is decoded, so that the panel never blinks, and its object URL
    // is released when the next one replaces it (an animation would otherwise pile the pictures up in the browser).
    const images = [0, 1].map(() => ctx.el("image", {preserveAspectRatio: "none", visibility: "hidden"}, panel));
    const urls = [null, null];
    let front = 1;
    const view = document.createElement("canvas");
    const show = (blob) => new Promise((resolve) => {
      const back = 1 - front, target = images[back], url = URL.createObjectURL(blob);
      let settled = false;
      const done = (ok) => {
        if (settled) return;
        settled = true;
        target.removeEventListener("load", onload);
        target.removeEventListener("error", onerror);
        if (ok) {
          target.setAttribute("visibility", "visible");
          images[front].setAttribute("visibility", "hidden");
          if (urls[front]) URL.revokeObjectURL(urls[front]);
          urls[back] = url;
          front = back;
        } else {
          URL.revokeObjectURL(url);
        }
        resolve();
      };
      const onload = () => done(true), onerror = () => done(false);
      target.addEventListener("load", onload);
      target.addEventListener("error", onerror);
      setTimeout(() => done(true), 500);  // An engine that fires no load event for an image still shows it
      target.setAttribute("href", url);
    });
    const pads = names.map((name) => {
      const colour = ctx.wireColor?.(component.at[name]) ?? "#8b8f94";
      const pin = ctx.netLabel?.(component.at[name]);  // The pin of the chip this terminal is wired to
      const pad = ctx.el("rect", {width: 7, height: 6, rx: 1, fill: colour, stroke: "rgb(0 0 0 / .5)", class: "bb-tft-pad", "data-terminal": name, "data-pin": pin ?? ""}, panel);
      const label = ctx.el("text", {"font-size": 5.5, "text-anchor": "start", fill: "#d6d9dd", class: "bb-tft-label"}, panel, pin ? `${name.toUpperCase()} ${pin}` : name.toUpperCase());
      ctx.el("title", {}, pad, pin ? `${name.toUpperCase()} is wired to ${pin}` : `${name.toUpperCase()}`);
      // A lead from the connector of the plate to its pin of the header on the board, in the colour of the connector
      const outline = ctx.el("path", {fill: "none", stroke: "rgb(0 0 0 / .45)", "stroke-width": 3.2, "stroke-linecap": "round"}, leads);
      const core = ctx.el("path", {fill: "none", stroke: colour, "stroke-width": 1.8, "stroke-linecap": "round"}, leads);
      return {pad, label, outline, core, name};
    });
    let panelWidth = 320, panelHeight = 240;
    let moveX = 0, moveY = 0;  // Where the user dragged the plate
    const place = () => {  // Same size for the longest side, whatever the orientation the firmware chose
      const scale = longest / Math.max(panelWidth, panelHeight);
      const w = panelWidth * scale, h = panelHeight * scale;
      const x = left + (right - left) / 2 - w / 2 - bezel, y = top - h - 2 * bezel - strip - 14;
      frame.setAttribute("x", x); frame.setAttribute("y", y);
      frame.setAttribute("width", w + 2 * bezel); frame.setAttribute("height", h + 2 * bezel + strip);
      for (const image of images) {
        image.setAttribute("x", x + bezel); image.setAttribute("y", y + bezel);
        image.setAttribute("width", w); image.setAttribute("height", h);
      }
      const edge = y + h + bezel;
      pads.forEach(({pad, label, outline, core, name}, i) => {
        const cx = x + (i + 0.5) * (w + 2 * bezel) / pads.length;
        pad.setAttribute("x", cx - 3.5); pad.setAttribute("y", edge + 2);
        label.setAttribute("x", cx + 2); label.setAttribute("y", edge + 11);
        label.setAttribute("transform", `rotate(90 ${cx + 2} ${edge + 11})`);
        const from = {x: cx + moveX, y: edge + strip - bezel + moveY}, to = ctx.at[name];
        const bend = Math.max(12, Math.abs(to.y - from.y) / 2);
        const d = `M ${from.x} ${from.y} C ${from.x} ${from.y + bend} ${to.x} ${to.y - bend} ${to.x} ${to.y}`;
        outline.setAttribute("d", d); core.setAttribute("d", d);
      });
    };
    place();
    ctx.floating?.(panel, component.id, (dx, dy) => { moveX = dx; moveY = dy; place(); });
    for (const [name, point] of Object.entries(ctx.at)) {
      ctx.el("circle", {cx: point.x, cy: point.y, r: 4, class: "bb-probe-ring"}, ctx.layer);
      ctx.el("title", {}, ctx.layer, `${component.id} ${name.toUpperCase()}`);
    }
    const title = ctx.el("title", {}, panel, `${component.id}: KMRTM28028 TFT (ILI9341)`);
    const canvas = document.createElement("canvas");
    canvas.width = 240;
    canvas.height = 320;
    const context = canvas.getContext("2d");
    const draw = async (shown) => {
      const bytes = Uint8Array.from(atob(shown.rgb565), (c) => c.charCodeAt(0));
      if (canvas.width !== shown.width || canvas.height !== shown.height) { canvas.width = shown.width; canvas.height = shown.height; }
      const pixels = context.createImageData(shown.width, shown.height);
      for (let i = 0; i < shown.width * shown.height; i++) {
        const word = (bytes[2 * i] << 8) | bytes[2 * i + 1];
        const r = (word >> 11) & 31, g = (word >> 5) & 63, b = word & 31;
        pixels.data.set([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255], 4 * i);
      }
      context.putImageData(pixels, 0, 0);
      // Portrait memory is turned a quarter turn to be shown in landscape.
      const turned = shown.height > shown.width;
      const width = turned ? shown.height : shown.width, height = turned ? shown.width : shown.height;
      if (view.width !== width || view.height !== height) { view.width = width; view.height = height; }
      const out = view.getContext("2d");
      out.setTransform(1, 0, 0, 1, 0, 0);
      if (turned) { out.translate(width, 0); out.rotate(Math.PI / 2); }
      out.drawImage(canvas, 0, 0);
      const blob = await new Promise((resolve) => view.toBlob(resolve, "image/png"));
      if (blob) await show(blob);
      if (width !== panelWidth || height !== panelHeight) {
        panelWidth = width; panelHeight = height;
        place();
        ctx.refit?.();
      }
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
          if (shown) await draw(shown);
          shownRevision = shown ? shown.revision : target;
          await new Promise((resolve) => setTimeout(resolve, 40));  // At most about 15 images a second, whatever the firmware draws
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
