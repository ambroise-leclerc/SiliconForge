// LCD1602 (HD44780): a row of header pins under a miniature of the module, which shows the characters of its controller dot
// by dot. The cells come from sf.screen when the revision state of the module changes; the contrast and the backlight come
// with every frame.
"use strict";

SiliconForge.components.define("lcd1602", {
  create(ctx, component) {
    const points = Object.values(ctx.at);
    const left = Math.min(...points.map((p) => p.x));
    const right = Math.max(...points.map((p) => p.x));
    const top = Math.min(...points.map((p) => p.y));
    ctx.layer.classList.add("bb-lcd");
    const blue = component.properties?.color === "blue";
    const colours = blue
      ? {lit: "#3d6cf0", dark: "#14275f", ink: "#eef3ff", grid: "rgb(255 255 255 / .07)"}
      : {lit: "#a8c94a", dark: "#55662a", ink: "#17230b", grid: "rgb(0 0 0 / .07)"};
    const columns = Number(component.properties?.columns ?? 16), rows = Number(component.properties?.rows ?? 2);
    // A dot is 1.8 units on a pitch of 2, a cell five dots by eight, with a gap between cells as on the glass.
    const dot = 1.8, pitch = 2, cellWidth = 5 * pitch + 2, cellHeight = 8 * pitch + 3;
    const glassWidth = columns * cellWidth - 2 + 12, glassHeight = rows * cellHeight - 3 + 12;
    const board = {width: glassWidth + 28, height: glassHeight + 22 + 36};
    // In the order of the header on the board (the keys of ctx.at come sorted by name): VSS, VDD, V0... K
    const names = Object.keys(ctx.at).sort((a, b) => ctx.at[a].x - ctx.at[b].x || ctx.at[a].y - ctx.at[b].y);
    // Above the chip and the probes (ctx.plate), so that a plate dragged over them stays in front.
    const host = ctx.plate?.(component.id) ?? ctx.layer;
    const leads = ctx.el("g", {class: "bb-lcd-leads", "pointer-events": "none"}, host);
    const plate = ctx.el("g", {class: "bb-lcd-module", "data-component": component.id}, host);
    const pcb = ctx.el("rect", {rx: 3, fill: "#1f6b3a", stroke: "#0f3d20"}, plate);
    const bezel = ctx.el("rect", {rx: 1.5, fill: "#151515"}, plate);
    const glass = ctx.el("rect", {fill: colours.dark}, plate);
    const grid = ctx.el("path", {fill: colours.grid}, plate);
    const ink = ctx.el("path", {fill: colours.ink}, plate);
    const pads = names.map((name) => {
      const colour = ctx.wireColor?.(component.at[name]) ?? "#c9a54a";
      const pin = ctx.netLabel?.(component.at[name]);  // The pin of the chip this terminal is wired to
      const pad = ctx.el("rect", {width: 7, height: 6, rx: 1, fill: colour, stroke: "rgb(0 0 0 / .5)", class: "bb-lcd-pad", "data-terminal": name, "data-pin": pin ?? ""}, plate);
      const label = ctx.el("text", {"font-size": 5.5, "text-anchor": "start", fill: "#e3e7ea", class: "bb-lcd-label"}, plate, pin ? `${name.toUpperCase()} ${pin}` : name.toUpperCase());
      ctx.el("title", {}, pad, pin ? `${name.toUpperCase()} is wired to ${pin}` : name.toUpperCase());
      // A lead from the connector of the plate to its pin of the header on the board, in the colour of the connector
      const outline = ctx.el("path", {fill: "none", stroke: "rgb(0 0 0 / .45)", "stroke-width": 3.2, "stroke-linecap": "round"}, leads);
      const core = ctx.el("path", {fill: "none", stroke: colour, "stroke-width": 1.8, "stroke-linecap": "round"}, leads);
      return {pad, label, outline, core, name};
    });
    let moveX = 0, moveY = 0;  // Where the user dragged the plate
    let origin = {x: 0, y: 0};  // Top-left corner of the first cell
    const place = () => {
      const x = left + (right - left) / 2 - board.width / 2, y = top - board.height - 14;
      pcb.setAttribute("x", x); pcb.setAttribute("y", y);
      pcb.setAttribute("width", board.width); pcb.setAttribute("height", board.height);
      bezel.setAttribute("x", x + 8); bezel.setAttribute("y", y + 8);
      bezel.setAttribute("width", glassWidth + 12); bezel.setAttribute("height", glassHeight + 6);
      glass.setAttribute("x", x + 14); glass.setAttribute("y", y + 11);
      glass.setAttribute("width", glassWidth); glass.setAttribute("height", glassHeight);
      origin = {x: x + 20, y: y + 17};
      const edge = y + board.height - 34;
      pads.forEach(({pad, label, outline, core, name}, i) => {
        const cx = x + 10 + (i + 0.5) * (board.width - 20) / pads.length;
        pad.setAttribute("x", cx - 3.5); pad.setAttribute("y", edge + 2);
        label.setAttribute("x", cx + 2); label.setAttribute("y", edge + 11);
        label.setAttribute("transform", `rotate(90 ${cx + 2} ${edge + 11})`);
        const from = {x: cx + moveX, y: y + board.height + moveY}, to = ctx.at[name];
        const bend = Math.max(12, Math.abs(to.y - from.y) / 2);
        const d = `M ${from.x} ${from.y} C ${from.x} ${from.y + bend} ${to.x} ${to.y - bend} ${to.x} ${to.y}`;
        outline.setAttribute("d", d); core.setAttribute("d", d);
      });
    };
    place();
    ctx.floating?.(plate, component.id, (dx, dy) => { moveX = dx; moveY = dy; place(); });
    for (const [name, point] of Object.entries(ctx.at)) {
      ctx.el("circle", {cx: point.x, cy: point.y, r: 4, class: "bb-probe-ring"}, ctx.layer);
      ctx.el("title", {}, ctx.layer, `${component.id} ${name.toUpperCase()}`);
    }
    const title = ctx.el("title", {}, plate, `${component.id}: LCD1602`);

    // The dots: one path for those that are on, one for the faint grid of every dot.
    const square = (x, y) => `M${x.toFixed(1)} ${y.toFixed(1)}h${dot}v${dot}h-${dot}z`;
    let gridPath = "";
    for (let row = 0; row < rows; ++row)
      for (let column = 0; column < columns; ++column)
        for (let r = 0; r < 8; ++r)
          for (let c = 0; c < 5; ++c) gridPath += square(column * cellWidth + c * pitch, row * cellHeight + r * pitch);
    let shown = null;        // The last answer of sf.screen
    let blinkPhase = false;  // The character at the cursor is replaced by a block in one phase out of two
    const draw = () => {
      grid.setAttribute("transform", `translate(${origin.x} ${origin.y})`);
      ink.setAttribute("transform", `translate(${origin.x} ${origin.y})`);
      grid.setAttribute("d", gridPath);
      let path = "";
      if (shown?.on) {
        shown.cells.forEach((rowsOfDots, index) => {
          const column = index % shown.columns, row = Math.floor(index / shown.columns);
          const at = (cell) => cell && cell[0] === column && cell[1] === row;
          const block = at(shown.blink) && blinkPhase;
          for (let r = 0; r < 8; ++r) {
            const bits = block || (r === 7 && at(shown.cursor)) ? 0x1f : rowsOfDots[r];
            for (let c = 0; c < 5; ++c)
              if (bits & (0x10 >> c)) path += square(column * cellWidth + c * pitch, row * cellHeight + r * pitch);
          }
        });
      }
      ink.setAttribute("d", path);
      plate.dataset.text = shown?.on ? shown.text.join("\n") : "";
    };
    draw();
    const blinking = setInterval(() => {  // The blink of the HD44780: about 0.4 s in each phase
      if (!plate.isConnected) { clearInterval(blinking); return; }  // Redrawn or removed
      blinkPhase = !blinkPhase;
      if (shown?.on && shown.blink) draw();
    }, 410);
    let shownRevision, fetching = false, wanted;
    const refresh = async () => {
      if (fetching) return;
      fetching = true;
      try {
        while (shownRevision !== wanted) {
          const target = wanted;
          const answer = await ctx.screen();
          if (answer) { shown = answer; draw(); }
          shownRevision = answer ? answer.revision : target;
          await new Promise((resolve) => setTimeout(resolve, 40));  // At most about 25 redraws a second
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
        const light = Math.max(0, Math.min(1, state.backlight ?? 0));
        glass.setAttribute("fill", light > 0 ? colours.lit : colours.dark);
        glass.setAttribute("fill-opacity", String(0.55 + 0.45 * light));
        const contrast = state.powered ? Math.max(0, Math.min(1, state.contrast ?? 0)) : 0;
        ink.setAttribute("fill-opacity", String(contrast));
        plate.dataset.contrast = String(contrast);
        plate.dataset.backlight = String(light);
        const problems = [state.early ? `${state.early} lost while busy` : "", state.timing ? `${state.timing} E timing violations` : ""].filter(Boolean);
        title.textContent = `${component.id}: ${!state.powered ? "not powered" : state.displayOn ? "on" : "off"}, ${state.dataBits ?? 8}-bit`
          + (problems.length ? `, ${problems.join(", ")}` : "");
      },
    };
  },
});
