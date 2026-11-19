// SiliconForge web interface. The C++ side (apps/siliconforge/WebBridge.hpp) pushes the state of the MCU and of the
// components of its circuit about 30 times per second through siliconForge.onState(json) and exposes the controls as
// sf.* C++ functions. The circuit is computed in C++ (ADR 0005): this page draws it and forwards the interactions.
//
// window.siliconForge is also the automation API used by UI tests (tests/ui): it can drive the firmware like a user,
// and wait for serial output or pin states in simulated time.
"use strict";

(() => {
  const call = (name, ...args) => webFront.cppFunction(name)(...args);
  const $ = (id) => document.getElementById(id);
  const eol = {"": "", "\\n": "\n", "\\r": "\r", "\\r\\n": "\r\n"};
  const pinState = {low: 0, high: 1, floating: 2};

  let state = null;
  let serial = "";
  let previousRegisters = new Map();
  let lastRate = null;  // Cycles and wall time of the last speed measurement
  let circuit = null;  // The circuit the chip is plugged in (sf.circuit()), null for a bare board
  let instruments = null;
  let bench = null;
  let terminal = null;
  let editor = null;
  let side = null;
  const forced = new Map();  // Pin name -> level forced from this page (0, 1)
  const waiters = new Set();

  // Automation API ------------------------------------------------------------------------------------------------
  const api = {
    get state() { return state; },
    get output() { return serial; },
    onState(json) {
      const next = JSON.parse(json);
      if (state && state.chipRevision !== next.chipRevision) {
        serial = ""; $("serial-output").textContent = "";
      }
      state = next;
      serial += state.serial;
      render(state);
      for (const waiter of [...waiters]) waiter();
    },
    chip: async () => JSON.parse(await call("sf.chip")),
    mcus: () => call("sf.mcus"),
    // The functions that load or select something reject with an Error whose `code` is one of the siliconforge.* codes
    // of WebBridge.hpp; for a description the message starts with the JSON path of the fault.
    loadMcu: (text) => call("sf.loadMcu", text),
    selectMcu: (name) => call("sf.selectMcu", name),
    /** The circuit the chip is plugged in, with the MCU pin of each component; null for a bare board. */
    circuit: async () => JSON.parse(await call("sf.circuit")),
    circuits: () => call("sf.circuits"),
    selectCircuit: (name) => call("sf.selectCircuit", name),
    loadCircuit: (name, text) => call("sf.loadCircuit", name, text),
    examples: async () => JSON.parse(await call("sf.examples")),
    /** Loads a built-in firmware with its circuit and runs it. */
    loadExample: async (id) => {
      await call("sf.loadExample", id);
      serial = "";
      $("serial-output").textContent = "";
    },
    /** State of a component of the circuit in the last frame: {brightness}, {pressed}, {position, voltage}... */
    component: (id) => state?.components?.[id],
    /** What the user does to a component: setComponent("button-d2", "pressed", 1), ("pot-a0", "position", 0.5). */
    setComponent: (id, name, value) => call("sf.component.set", id, name, Number(value)),
    /**
     * Moves terminals of a component, or of a wire ("wire-3"), to other holes: moveComponent("r-d13", {b: "h20"}).
     * The terminals not named stay. Rejects with siliconforge.invalidPlacement and the rule broken (length, hole taken...).
     */
    moveComponent: (id, holes) => call("sf.component.move", id, JSON.stringify(holes)),
    get instruments() { return instruments; },
    /** The bench: `list()`, `run(scenarioFile)`, `open(runName)`, the shown `result`; see ui/bench.js. */
    get bench() { return bench; },
    /** The agent terminal: `clients`, `open(id)`, `hangUp()`, `state`; see ui/terminal.js. */
    get terminal() { return terminal; },
    /** The editor of the project sources: `files`, `open(path)`, `save()`, `state`; see ui/editor.js. */
    get editor() { return editor; },
    /** The tabs of the side column: `select("bench" | "terminal" | "editor")`, `selected`. */
    get side() { return side; },
    run: () => call("sf.run"),
    pause: () => call("sf.pause"),
    step: () => call("sf.step"),
    reset: async () => {
      serial = "";
      $("serial-output").textContent = "";
      await call("sf.reset");
    },
    setSpeed: (factor) => call("sf.setSpeed", factor),
    send: (text) => call("sf.serialSend", text),
    setPin: (name, level) => call("sf.setPin", name, level === undefined || level === null ? -1 : Number(level)),
    setVoltage: (name, volts) => call("sf.setVoltage", name, volts),
    /** Voltage on a pin in the last frame. */
    voltage: (name) => api.pin(name)?.volts ?? 0,
    load: async (name, bytes) => {
      await call("sf.loadFirmware", name, bytes);
      serial = "";
      $("serial-output").textContent = "";
    },
    pin: (name) => state?.pins.find((pin) => pin.name === name || pin.label === name),
    /** Resolves when predicate(state) holds on a received frame, rejects after timeoutMs of wall time. */
    waitFor(predicate, timeoutMs = 5000) {
      return new Promise((resolve, reject) => {
        const check = () => {
          if (state && predicate(state)) {
            waiters.delete(check);
            clearTimeout(timer);
            resolve(state);
          }
        };
        const timer = setTimeout(() => {
          waiters.delete(check);
          reject(new Error(`siliconForge.waitFor: timeout after ${timeoutMs} ms`));
        }, timeoutMs);
        waiters.add(check);
        check();
      });
    },
    waitForSerial(text, timeoutMs) { return api.waitFor(() => serial.includes(text), timeoutMs); },
    waitForPin(name, level, timeoutMs) { return api.waitFor(() => api.pin(name)?.state === level, timeoutMs); },
    pinState,
  };
  window.siliconForge = api;

  // Rendering -----------------------------------------------------------------------------------------------------
  function formatTime(seconds) {
    return `${seconds.toFixed(6)} s`;
  }

  function renderStatus(s) {
    const status = $("status");
    if (s.running) {
      status.textContent = "running";
      status.dataset.state = "running";
    } else if (s.stop && s.stop !== "budget") {
      status.textContent = `stopped: ${s.stop} at ${hex(s.pc, 4)}${s.symbol ? ` (${s.symbol})` : ""}`;
      status.dataset.state = "stopped";
    } else {
      status.textContent = "paused";
      status.dataset.state = "paused";
    }
    $("run").textContent = s.running ? "Pause" : "Run";
    $("chip").textContent = `${s.mcu} · ${s.clockHz / 1e6} MHz`;
    syncMcuSelection(s);
    $("firmware").textContent = s.firmware || "no firmware";
    $("time").textContent = formatTime(s.cycles / s.clockHz);
    $("cycles").textContent = `${s.cycles.toLocaleString()} cycles`;

    const now = performance.now();
    if (!lastRate || s.cycles < lastRate.cycles) {
      lastRate = {cycles: s.cycles, at: now};
    } else if (now - lastRate.at > 500) {
      const ratio = (s.cycles - lastRate.cycles) / s.clockHz / ((now - lastRate.at) / 1000);
      $("rate").textContent = s.running && ratio > 0 ? `×${ratio.toFixed(ratio < 10 ? 2 : 0)} real time` : "";
      lastRate = {cycles: s.cycles, at: now};
    }
  }

  function pinElement(pin, analog) {
    const element = document.createElement("div");
    element.className = "pin";
    element.dataset.pin = pin.name;
    for (const [kind, text] of [["label", pin.label || pin.name], ["port", pin.name], ["mode", ""], ["level", ""]]) {
      const span = document.createElement("span"); span.className = kind; span.textContent = text; element.append(span);
    }
    element.title = "Click to drive this input";
    element.addEventListener("click", (event) => {
      if (event.target.tagName === "INPUT") return;
      cycleInput(pin.name);
    });
    if (analog) {
      const slider = document.createElement("input");
      slider.type = "range";
      slider.min = 0;
      slider.max = 5;
      slider.step = 0.01;
      slider.value = 0;
      slider.title = "Analog voltage";
      slider.addEventListener("input", () => {
        forced.delete(pin.name);
        api.setVoltage(pin.name, Number(slider.value));
        slider.title = `${Number(slider.value).toFixed(2)} V`;
      });
      element.append(slider);
    }
    return element;
  }

  /** Drives an input pin in turn high, low, then releases it; outputs are left alone. */
  function cycleInput(name) {
    if (api.pin(name)?.output) return;
    const current = forced.get(name);
    const next = current === undefined ? 1 : current === 1 ? 0 : undefined;
    if (next === undefined) forced.delete(name);
    else forced.set(name, next);
    api.setPin(name, next);
  }

  // Board views (breadboard, pinout) built from the chip description and the circuit, once a state names them -----
  const viewContext = {
    pin: (name) => api.pin(name),
    cycle: cycleInput,
    forced: (name) => (forced.has(name) ? (forced.get(name) ? "high" : "low") : ""),
    setPin: (name, level) => api.setPin(name, level),
    setComponent: (id, name, value) => api.setComponent(id, name, value).catch((error) => console.warn(error.message)),
    moveComponent: (id, holes) => api.moveComponent(id, holes).then(() => showCircuitError(null), (error) => {
      showCircuitError(new Error(`Cannot move ${id}: ${error.message}`));
      throw error;
    }),
  };
  let views = null;

  let boardRevision = null;  // Chip and circuit revisions the views were built for
  let chipRequest = 0;
  let pendingRevision = null;
  let chipRetryAt = 0;
  function renderViews(s) {
    const revision = `${s.chipRevision}:${s.circuitRevision}`;
    if (boardRevision !== revision && pendingRevision !== revision && performance.now() >= chipRetryAt) {
      pendingRevision = revision;
      const request = ++chipRequest;
      const chipChanged = !boardRevision || boardRevision.split(":")[0] !== String(s.chipRevision);
      views = [];
      forced.clear();
      if (chipChanged) {
        $("digital").replaceChildren();
        $("analog").replaceChildren();
      }
      for (const name of ["breadboard", "pinout"]) {
        const container = $(name);
        container.querySelectorAll("svg").forEach((svg) => svg.remove());
        container.querySelector(".hint").textContent = "Loading chip description…";
      }
      Promise.all([api.chip(), api.circuit()]).then(([chip, plugged]) => {
        if (request !== chipRequest || `${state?.chipRevision}:${state?.circuitRevision}` !== revision) return;
        circuit = plugged;
        $("breadboard").querySelector(".hint").textContent = !chip ? `No package description for ${s.mcu}: see the Pins tab.`
          : plugged ? `${plugged.description || plugged.name} Click a chip pin to drive it.`
          : "A bare board: no circuit is plugged for this microcontroller. Click a chip pin to drive it.";
        $("pinout").querySelector(".hint").textContent = chip
          ? "Click a port to drive it: released → high → low → released." : `No package description for ${s.mcu}: see the Pins tab.`;
        if (chip) {
          views.push(window.SiliconForgeViews.breadboard($("breadboard"), chip, viewContext, plugged));
          views.push(window.SiliconForgeViews.pinout($("pinout"), chip, viewContext));
        }
        syncCircuitSelection(plugged);
        boardRevision = revision;
        chipRetryAt = 0;
        showMcuError(null);
        for (const view of views) view.update(state);
      }).catch((error) => {
        if (request !== chipRequest) return;
        chipRetryAt = performance.now() + 1000;
        showMcuError(error);
      }).finally(() => {
        if (request === chipRequest) pendingRevision = null;
      });
    }
    for (const view of views || []) view.update(s);
  }

  function syncCircuitSelection(plugged) {
    const select = $("circuit-select");
    select.querySelector("option[data-loaded]")?.remove();
    const id = plugged?.id ?? "";
    $("circuit-name").textContent = plugged?.name ?? "";
    if (![...select.options].some((option) => option.value === id)) {
      const loaded = new Option(plugged ? `${plugged.name} (loaded)` : "bare board", id);
      loaded.dataset.loaded = "true";
      select.add(loaded);
    }
    select.value = id;
  }

  function showShorts(shorts) {
    const banner = $("shorts");
    const text = (shorts ?? []).join(" · ");
    banner.hidden = !text;
    if (banner.textContent !== text) banner.textContent = text;
  }

  function syncMcuSelection(s) {
    const select = $("mcu-select");
    const known = s.mcuId === null ? null : [...select.options].find((option) => !option.dataset.loaded && option.value === s.mcuId);
    let loaded = select.querySelector("option[data-loaded]");
    if (known) {
      loaded?.remove();
      if (select.value !== known.value) select.value = known.value;
    } else {
      if (!loaded) {
        loaded = new Option(`${s.mcu} (loaded)`, "__loaded__");
        loaded.dataset.loaded = "true";
        loaded.disabled = true;
        select.add(loaded);
      }
      const label = `${s.mcu} (loaded)`;
      if (loaded.textContent !== label) loaded.textContent = label;
      if (select.value !== loaded.value) select.value = loaded.value;
    }
  }

  function showMcuError(error) {
    $("mcu-error").hidden = !error;
    $("mcu-error").textContent = error?.message ?? "";
  }

  function showFirmwareError(error) {
    $("firmware-error").hidden = !error;
    $("firmware-error").textContent = error?.message ?? "";
  }

  function showCircuitError(error) {
    $("circuit-error").hidden = !error;
    $("circuit-error").textContent = error?.message ?? "";
  }

  function applyTheme(theme) {
    document.documentElement.dataset.theme = theme;
  }

  function selectView(name) {
    for (const element of document.querySelectorAll("[data-view]")) {
      const selected = element.dataset.view === name;
      element.classList.toggle("selected", selected);
      if (element.getAttribute("role") === "tab") element.setAttribute("aria-selected", selected);
    }
    try { localStorage.setItem("siliconforge.view", name); } catch { /* Storage may be unavailable */ }
  }

  function renderPins(pins) {
    const digital = $("digital");
    const analog = $("analog");
    if (!digital.childElementCount) {
      const analogLabel = (pin) => /^A\d+$/.test(pin.label);
      const labels = new Intl.Collator(undefined, {numeric: true});
      const order = (a, b) => labels.compare(a.label || a.name, b.label || b.name);
      pins.filter((p) => !analogLabel(p)).sort(order).forEach((p) => digital.append(pinElement(p, false)));
      pins.filter(analogLabel).sort(order).forEach((p) => analog.append(pinElement(p, true)));
    }
    for (const pin of pins) {
      const element = document.querySelector(`.pin[data-pin="${CSS.escape(pin.name)}"]`);
      if (!element) continue;
      const pwm = pin.output && pin.edges >= 2;  // Several edges in one frame: show the average level
      element.dataset.state = pin.state;
      element.dataset.output = pin.output;
      element.dataset.pwm = pwm;
      element.style.setProperty("--duty", `${(pin.duty * 100).toFixed(1)}%`);
      element.dataset.forced = forced.has(pin.name) ? (forced.get(pin.name) ? "high" : "low") : "";
      const slider = element.querySelector("input[type=range]");
      if (slider && document.activeElement !== slider && !pin.output) {
        slider.value = pin.volts;
        slider.title = `${pin.volts.toFixed(2)} V`;
      }
      element.querySelector(".mode").textContent = pin.output
        ? (pwm ? `OUT ${Math.round(pin.duty * 100)}%` : "OUT")
        : pin.pullUp ? "IN pull-up" : "IN";
    }
    const led = pins.find((pin) => pin.label === "D13");
    if (led) $("led-l").querySelector(".lamp").style.setProperty("--level", led.output ? led.duty : 0);
  }

  function hex(value, digits) {
    return "0x" + value.toString(16).toUpperCase().padStart(digits, "0");
  }

  function renderCpu(s) {
    $("arch").textContent = s.arch.toUpperCase();
    const special = s.registers.filter(([name]) => !/^R\d+$/.test(name));
    $("special").innerHTML = special
      .map(([name, value, bits]) => `<span><b>${name}</b>${hex(value, bits / 4)}${name === "PC" && s.symbol ? ` <span class="muted">${s.symbol}</span>` : ""}</span>`)
      .join("");
    const sreg = special.find(([name]) => name === "SREG")?.[1] ?? 0;
    $("flags").innerHTML = [..."ITHSVNZC"].map((flag, i) => `<span class="${sreg & (0x80 >> i) ? "on" : ""}">${flag}</span>`).join("");
    const registers = s.registers.filter(([name]) => /^R\d+$/.test(name));
    $("registers").innerHTML = registers
      .map(([name, value]) => `<div class="${previousRegisters.get(name) !== value ? "changed" : ""}"><b>${name}</b>${hex(value, 2).slice(2)}</div>`)
      .join("");
    previousRegisters = new Map(registers.map(([name, value]) => [name, value]));
  }

  function flash(id, active) {
    $(id).querySelector(".lamp").style.setProperty("--level", active ? 1 : 0);
  }

  function renderSerial(chunk) {
    flash("led-tx", chunk.length > 0);
    if (!chunk) return;
    const output = $("serial-output");
    const atBottom = output.scrollTop + output.clientHeight >= output.scrollHeight - 4;
    output.textContent += chunk;
    if (output.textContent.length > 200000) output.textContent = output.textContent.slice(-150000);
    if (atBottom) output.scrollTop = output.scrollHeight;
  }

  function render(s) {
    renderStatus(s);
    renderPins(s.pins);
    renderViews(s);
    renderCpu(s);
    renderSerial(s.serial);
    showShorts(s.shorts);
    instruments?.update();
  }

  // Controls ------------------------------------------------------------------------------------------------------
  function bindControls() {
    for (const tab of document.querySelectorAll(".tabs [data-view]")) tab.addEventListener("click", () => selectView(tab.dataset.view));
    let view = "breadboard";
    try { view = localStorage.getItem("siliconforge.view") || view; } catch { /* Storage may be unavailable */ }
    selectView(view);
    $("run").addEventListener("click", () => (state?.running ? api.pause() : api.run()));
    $("step").addEventListener("click", () => api.step());
    $("reset").addEventListener("click", () => api.reset());
    $("speed").addEventListener("change", (event) => api.setSpeed(Number(event.target.value)));
    const loadFirmwareFile = async (file) => {
      try {
        await api.load(file.name, new Uint8Array(await file.arrayBuffer()));
        showFirmwareError(null);
        await api.run();
      } catch (error) {
        showFirmwareError(new Error(`Cannot load ${file.name}: ${error.message}`));
      }
    };
    const loadMcuFile = async (file) => {
      if (!file) return;
      try { await api.loadMcu(await file.text()); showMcuError(null); }
      catch (error) { showMcuError(error); }
    };
    $("mcu-file").addEventListener("change", async (event) => {
      await loadMcuFile(event.target.files[0]); event.target.value = "";
    });
    $("mcu-select").addEventListener("change", async (event) => {
      try { await api.selectMcu(event.target.value); showMcuError(null); }
      catch (error) { showMcuError(error); }
    });
    const loadCircuitFile = async (file, text) => {
      try { await api.loadCircuit(file.name, text ?? await file.text()); showCircuitError(null); }
      catch (error) { showCircuitError(new Error(`Cannot load ${file.name}: ${error.message}`)); }
    };
    $("circuit-file").addEventListener("change", async (event) => {
      if (event.target.files[0]) await loadCircuitFile(event.target.files[0]);
      event.target.value = "";
    });
    $("circuit-select").addEventListener("change", async (event) => {
      try { await api.selectCircuit(event.target.value); showCircuitError(null); }
      catch (error) { showCircuitError(error); syncCircuitSelection(circuit); }
    });
    $("example-select").addEventListener("change", async (event) => {
      const id = event.target.value;
      event.target.value = "";
      if (!id) return;
      try { await api.loadExample(id); showFirmwareError(null); showCircuitError(null); }
      catch (error) { showFirmwareError(error); }
    });
    $("theme").addEventListener("click", () => {
      const next = document.documentElement.dataset.theme === "light" ? "dark" : "light";
      applyTheme(next);
      try { localStorage.setItem("siliconforge.theme", next); } catch { /* Storage may be unavailable */ }
    });
    document.addEventListener("dragover", (event) => { event.preventDefault(); });
    // A dropped JSON file is a circuit when it has a board, an MCU description otherwise; anything else is a firmware.
    document.addEventListener("drop", async (event) => {
      event.preventDefault();
      const file = event.dataTransfer.files[0];
      if (!file) return;
      if (!file.name.toLowerCase().endsWith(".json")) return loadFirmwareFile(file);
      const text = await file.text();
      let isCircuit = false;
      try { isCircuit = "board" in JSON.parse(text); } catch { /* Reported by the loader */ }
      if (isCircuit) await loadCircuitFile(file, text); else await loadMcuFile(file);
    });
    $("firmware-file").addEventListener("change", async (event) => {
      if (event.target.files[0]) await loadFirmwareFile(event.target.files[0]);
      event.target.value = "";
    });
    $("serial-form").addEventListener("submit", (event) => {
      event.preventDefault();
      const text = $("serial-text").value + eol[$("serial-eol").value];
      if (!text) return;
      api.send(text);
      flash("led-rx", true);
      setTimeout(() => flash("led-rx", false), 120);
      $("serial-text").value = "";
    });
    $("serial-clear").addEventListener("click", () => ($("serial-output").textContent = ""));
    document.addEventListener("keydown", (event) => {
      if (event.target.closest("input, select, textarea, [role=tab]")) return;
      if (event.key === " ") { event.preventDefault(); $("run").click(); }
      else if (event.key === "s") api.step();
      else if (event.key === "r") api.reset();
    });
  }

  /**
   * The side column holds three tabs: the bench (bench, serial monitor and CPU), the agent terminal and the sources.
   * The terminal and the sources widen the column, and the board and the instruments narrow. The tab chosen last is
   * remembered by this browser.
   */
  function sideTabs() {
    const tabs = [...document.querySelectorAll("#side [role=tab]")];
    const panes = {bench: $("side-bench"), terminal: $("terminal"), editor: $("editor")};
    let selected = "bench";
    const controller = {
      get selected() { return selected; },
      select(name) {
        if (!(name in panes)) throw new Error(`No side tab ${name}`);
        selected = name;
        for (const tab of tabs) {
          const on = tab.dataset.side === name;
          tab.setAttribute("aria-selected", String(on));
          tab.tabIndex = on ? 0 : -1;
        }
        panes.bench.hidden = name !== "bench";
        // The panels of the terminal and the editor do their own work when they are shown or hidden.
        terminal?.display(name === "terminal");
        editor?.display(name === "editor");
        document.querySelector(".workspace").classList.toggle("wide", name !== "bench");
        try { localStorage.setItem("siliconforge.side", name); } catch { /* Storage may be unavailable */ }
      },
      restore() {
        let name = "bench";
        try { name = localStorage.getItem("siliconforge.side") || name; } catch { /* Storage may be unavailable */ }
        controller.select(name in panes ? name : "bench");
      },
    };
    for (const tab of tabs) {
      tab.addEventListener("click", () => controller.select(tab.dataset.side));
      // The arrows, Home and End move between the tabs (ARIA tabs pattern, automatic activation).
      tab.addEventListener("keydown", (event) => {
        const index = tabs.indexOf(tab);
        const next = {ArrowRight: index + 1, ArrowLeft: index - 1, Home: 0, End: tabs.length - 1}[event.key];
        if (next === undefined) return;
        event.preventDefault();
        const target = tabs[(next + tabs.length) % tabs.length];
        controller.select(target.dataset.side);
        target.focus();
      });
    }
    return controller;
  }

  let theme = window.matchMedia?.("(prefers-color-scheme: light)").matches ? "light" : "dark";
  try { theme = localStorage.getItem("siliconforge.theme") || theme; } catch { /* Storage may be unavailable */ }
  applyTheme(theme);
  instruments = window.SiliconForgeInstruments($("instruments"), {call, get state() { return state; }, get circuit() { return circuit; }});
  bench = window.SiliconForgeBench($("bench"), {call, ready: webFront.ready});
  side = sideTabs();
  terminal = window.SiliconForgeTerminal($("terminal"), {call, ready: webFront.ready, select: (visible) => side.select(visible ? "terminal" : "bench")});
  editor = window.SiliconForgeEditor($("editor"), {call, ready: webFront.ready, select: (visible) => side.select(visible ? "editor" : "bench")});
  side.restore();
  bindControls();
  webFront.ready.then(() => {
    $("status").textContent = "connected";
    api.circuits().then((names) => {
      $("circuit-select").replaceChildren(...names.map((name) => new Option(name, name)));
      if (boardRevision) syncCircuitSelection(circuit);
    }).catch(showCircuitError);
    api.examples().then((examples) => {
      const placeholder = new Option("Open an example…", "");
      $("example-select").replaceChildren(placeholder, ...examples.map((example) => {
        const option = new Option(example.title, example.id);
        option.title = example.description;
        return option;
      }));
    }).catch(showFirmwareError);
    api.mcus().then((names) => {
      const placeholder = new Option("Choose MCU…", ""); placeholder.disabled = true; placeholder.selected = true;
      $("mcu-select").replaceChildren(placeholder, ...names.map((name) => new Option(name, name)));
      if (state) syncMcuSelection(state);
    }).catch(showMcuError);
  }, (error) => {
    $("status").textContent = `disconnected: ${error.message ?? error}`;
    $("status").dataset.state = "stopped";
  });
})();
