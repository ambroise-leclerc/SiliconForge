// The devices of the electronics bench in the bottom panel: logic analyser, oscilloscope, programmable supply,
// variable load and multimeter. A bar selects what is shown: a click switches to one device, Shift- or Ctrl-click (or
// the "Side by side" switch) shows several at once. The analyser and the oscilloscope are two faces of the same
// capture (ui/instruments.js), so they exclude each other.
//
// The supply, the load and the multimeter are the components of the circuit (components/<type>/): a faceplate is
// built for each one, shows what the electrical model computes and sends the settings through sf.component.set. The
// jacks of a faceplate carry the symbol and the colour of the marker the breadboard draws on the hole where the lead is
// connected (components/<type>/render.js), with the name of that hole.
"use strict";

window.SiliconForgeDevices = (root, host) => {
  const $ = (selector) => root.querySelector(selector);
  const bar = $(".device-bar");
  const groups = {
    analyser: $("[data-device-group=analyser]"),
    supply: $("[data-device-group=supply]"),
    load: $("[data-device-group=load]"),
    multimeter: $("[data-device-group=multimeter]"),
  };
  const groupOf = (device) => (device === "logic" || device === "scope" ? "analyser" : device);
  const all = ["logic", "scope", "supply", "load", "multimeter"];
  const types = {supply: "bench-supply", load: "variable-load", multimeter: "multimeter"};
  const names = {
    supply: "programmable supply",
    load: "variable load",
    multimeter: "multimeter",
  };

  const h = (tag, attributes = {}, ...children) => {
    const element = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) {
      if (key === "class") element.className = value;
      else element.setAttribute(key, value);
    }
    element.append(...children);
    return element;
  };

  // Symbols and colours: the ones of the markers on the breadboard ----------------------------------------------------
  const symbols = {plus: "+", minus: "−", volt: "V", load: "Ω", com: "ground"};
  const symbolNode = (kind) => {
    if (symbols[kind] !== "ground") return h("span", {class: "jack-symbol"}, symbols[kind]);
    const NS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("viewBox", "-8 -8 16 16");
    svg.setAttribute("class", "jack-symbol");
    svg.setAttribute("aria-hidden", "true");
    const path = document.createElementNS(NS, "path");
    path.setAttribute("d", "M0 -5 V-1 M-5 -1 H5 M-3.2 2 H3.2 M-1.4 5 H1.4");
    svg.append(path);
    return svg;
  };
  const jack = (kind, label, hole) =>
    h("span", {class: `jack ${kind}`, title: `${label}: connected to hole ${hole}`},
      h("span", {class: "jack-dot"}, symbolNode(kind)),
      h("span", {class: "jack-label"}, label, h("small", {}, `→ ${hole}`)));

  // Formatting ----------------------------------------------------------------------------------------------------
  const volts = (value) => `${(value ?? 0).toFixed(3)} V`;
  const amps = (value) => {
    const magnitude = Math.abs(value ?? 0);
    return magnitude >= 1 ? `${(value).toFixed(3)} A` : `${((value ?? 0) * 1000).toFixed(1)} mA`;
  };
  const watts = (value) => `${((value ?? 0) * 1000).toFixed(1)} mW`;
  const ohms = (value) => (value >= 1e6 ? `${value / 1e6} MΩ` : value >= 1e3 ? `${value / 1e3} kΩ` : `${value} Ω`);

  // Faceplates -----------------------------------------------------------------------------------------------------
  const plates = new Map();  // Component id -> {element, update(state)}
  const lcd = (read, label) => h("div", {class: "lcd"}, h("small", {}, label), h("output", {"data-read": read}, "—"));
  const field = (label, unit, name, attributes, commit) => {
    const input = h("input", {type: "number", "aria-label": `${label} (${unit})`, "data-set": name, ...attributes});
    input.addEventListener("change", () => {
      const value = Math.min(Number(attributes.max), Math.max(Number(attributes.min), Number(input.value)));
      if (Number.isFinite(value)) commit(value);
    });
    return {input, row: h("label", {class: "knob"}, h("span", {}, label), input, h("small", {}, unit))};
  };
  const readInto = (element, read, text) => { element.querySelector(`[data-read=${read}]`).textContent = text; };
  const sync = (input, value) => { if (document.activeElement !== input && value !== undefined) input.value = String(Number(value.toFixed(6))); };

  function supplyPlate(component) {
    const set = (name) => (value) => host.setComponent(component.id, name, value);
    const voltage = field("Voltage", "V", "voltage", {min: 0, max: 30, step: 0.01}, set("voltage"));
    const limit = field("Limit", "mA", "currentLimit", {min: 1, max: 3000, step: 1}, (milli) => set("currentLimit")(milli / 1000));
    const output = h("button", {class: "output", type: "button", "aria-pressed": "false"}, "Output");
    output.addEventListener("click", () => set("enabled")(output.getAttribute("aria-pressed") === "true" ? 0 : 1));
    const element = h("article", {class: "faceplate supply", "data-id": component.id, "aria-label": `${component.id}: programmable supply`},
      h("header", {}, h("b", {}, "Programmable supply"), h("span", {class: "muted"}, component.id)),
      h("div", {class: "displays"}, lcd("v", "V"), lcd("i", "A"),
        h("div", {class: "modes"}, h("span", {class: "mode-led cv", "data-led": "cv"}, "CV"), h("span", {class: "mode-led cc", "data-led": "cc"}, "CC"))),
      h("div", {class: "knobs"}, voltage.row, limit.row, output),
      h("footer", {}, jack("plus", "+", component.at.positive), jack("minus", "−", component.at.negative)));
    return {
      element,
      update(state) {
        const on = Boolean(state.enabled);
        readInto(element, "v", on ? volts(state.outputVoltage) : "off");
        readInto(element, "i", on ? amps(state.outputCurrent) : "—");
        element.dataset.mode = ["off", "cv", "cc"][state.mode ?? 0];
        output.setAttribute("aria-pressed", String(on));
        sync(voltage.input, state.voltage);
        sync(limit.input, state.currentLimit === undefined ? undefined : state.currentLimit * 1000);
      },
    };
  }

  function loadPlate(component) {
    const resistance = field("Resistance", "Ω", "resistance", {min: 0.1, max: 1e8, step: "any"}, (value) => host.setComponent(component.id, "resistance", value));
    const presets = h("div", {class: "presets", role: "group", "aria-label": "Resistance presets"});
    for (const value of [10, 25, 50, 100, 220, 1000]) {
      const button = h("button", {class: "small", type: "button"}, ohms(value));
      button.addEventListener("click", () => host.setComponent(component.id, "resistance", value));
      presets.append(button);
    }
    const element = h("article", {class: "faceplate load", "data-id": component.id, "aria-label": `${component.id}: variable load`},
      h("header", {}, h("b", {}, "Variable load"), h("span", {class: "muted"}, component.id)),
      h("div", {class: "displays"}, lcd("v", "V"), lcd("i", "A"), lcd("p", "W")),
      h("div", {class: "knobs"}, resistance.row, presets),
      h("footer", {}, jack("load", "a", component.at.a), jack("load", "b", component.at.b)));
    return {
      element,
      update(state) {
        readInto(element, "v", volts(state.voltage));
        readInto(element, "i", amps(state.current));
        readInto(element, "p", watts((state.voltage ?? 0) * (state.current ?? 0)));
        sync(resistance.input, state.resistance);
      },
    };
  }

  function multimeterPlate(component) {
    const element = h("article", {class: "faceplate multimeter", "data-id": component.id, "aria-label": `${component.id}: multimeter`},
      h("header", {}, h("b", {}, "Multimeter"), h("span", {class: "muted"}, component.id)),
      h("div", {class: "displays"}, h("div", {class: "lcd big"}, h("small", {}, "DC V"), h("output", {"data-read": "v"}, "—"))),
      h("footer", {}, jack("volt", "V", component.at.v), jack("com", "COM", component.at.com)));
    return {element, update(state) { readInto(element, "v", volts(state.voltage)); }};
  }

  const builders = {supply: supplyPlate, load: loadPlate, multimeter: multimeterPlate};
  let built = null;  // The circuit the faceplates were built for

  function rebuild() {
    built = host.circuit;
    plates.clear();
    for (const device of Object.keys(types)) {
      const body = groups[device].querySelector(".plates");
      body.replaceChildren();
      const found = (built?.components ?? []).filter((component) => component.type === types[device]);
      for (const component of found) {
        const plate = builders[device](component);
        plates.set(component.id, plate);
        body.append(plate.element);
      }
      groups[device].querySelector(".empty").hidden = found.length > 0;
      const button = bar.querySelector(`[data-device=${device}]`);
      button.dataset.count = String(found.length);
      button.title = found.length ? `${found.length} ${names[device]}${found.length > 1 ? "s" : ""} in this circuit` : `No ${names[device]} in this circuit`;
    }
    for (const plate of plates.values()) plate.update({});
  }

  // Selection ----------------------------------------------------------------------------------------------------
  let visible = new Set(["logic"]);
  let tile = false;
  const mode = () => (host.instruments?.view.mode === "scope" ? "scope" : "logic");

  function apply() {
    const analyser = [...visible].find((device) => groupOf(device) === "analyser");
    if (analyser && host.instruments && host.instruments.view.mode !== analyser) host.instruments.set({mode: analyser});
    for (const [name, element] of Object.entries(groups)) element.hidden = !(name === "analyser" ? analyser : visible.has(name));
    for (const button of bar.querySelectorAll("[data-device]")) button.setAttribute("aria-pressed", String(visible.has(button.dataset.device)));
    root.dataset.devices = String(visible.size);
    bar.querySelector("[data-action=tile]").setAttribute("aria-pressed", String(tile));
    try { localStorage.setItem("siliconforge.devices", JSON.stringify({visible: [...visible], tile})); } catch { /* Storage may be unavailable */ }
  }

  /** Shows exactly these devices; the analyser and the oscilloscope exclude each other, the last one named wins. */
  function show(devices) {
    const next = [];
    for (const device of devices) {
      if (!all.includes(device)) throw new Error(`Unknown device: ${device}`);
      const index = next.findIndex((other) => groupOf(other) === groupOf(device));
      if (index >= 0) next.splice(index, 1);
      next.push(device);
    }
    visible = new Set(next.length ? next : [mode()]);
    apply();
    for (const plate of plates.values()) plate.update(host.state?.components?.[plate.element.dataset.id] ?? {});
  }

  function choose(device, additive) {
    if (!additive && !tile) { show([device]); return; }
    if (visible.has(device)) { if (visible.size > 1) show([...visible].filter((other) => other !== device)); return; }
    show([...visible, device]);
  }

  bar.addEventListener("click", (event) => {
    const button = event.target.closest("button");
    if (!button) return;
    if (button.dataset.action === "tile") {
      tile = !tile;
      if (!tile && visible.size > 1) show([[...visible].pop()]); else apply();
      return;
    }
    if (button.dataset.device) choose(button.dataset.device, event.shiftKey || event.ctrlKey || event.metaKey);
  });

  try {
    const saved = JSON.parse(localStorage.getItem("siliconforge.devices") ?? "null");
    if (saved && Array.isArray(saved.visible)) {
      tile = Boolean(saved.tile);
      const valid = saved.visible.filter((device) => all.includes(device));
      visible = new Set(valid.length ? valid : [mode()]);
    } else {
      visible = new Set([mode()]);
    }
  } catch { visible = new Set([mode()]); }
  // The inner tabs of the analyser are replaced by the bar.
  const innerTabs = $("[data-device-group=analyser] .tabs");
  if (innerTabs) innerTabs.hidden = true;
  apply();

  return {
    /** Called on every state frame: rebuilds the faceplates for a new circuit, then shows the model's values. */
    update() {
      if (host.circuit !== built) rebuild();
      for (const [id, plate] of plates) {
        const state = host.state?.components?.[id];
        if (state) plate.update(state);
      }
      const current = visible.has("scope") ? "scope" : visible.has("logic") ? "logic" : null;
      if (current && host.instruments && host.instruments.view.mode !== current) host.instruments.set({mode: current});
    },
    show,
    get visible() { return [...visible]; },
    get analyserVisible() { return visible.has("logic") || visible.has("scope"); },
    /** The element of the faceplate of a component, for tests and automation. */
    faceplate: (id) => plates.get(id)?.element ?? null,
  };
};
