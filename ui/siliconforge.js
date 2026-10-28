// SiliconForge web interface. The C++ side (apps/siliconforge/WebBridge.hpp) pushes the MCU state about 30 times
// per second through siliconForge.onState(json) and exposes the controls as sf.* C++ functions.
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
  const forced = new Map();  // Pin name -> level forced from this page (0, 1)
  const waiters = new Set();

  // Automation API ------------------------------------------------------------------------------------------------
  const api = {
    get state() { return state; },
    get output() { return serial; },
    onState(json) {
      state = JSON.parse(json);
      serial += state.serial;
      render(state);
      for (const waiter of [...waiters]) waiter();
    },
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
    load: async (name, bytes) => {
      const error = await call("sf.loadFirmware", name, bytes);
      if (error) throw new Error(error);
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
    element.innerHTML = `<span class="label">${pin.label || pin.name}</span><span class="port">${pin.name}</span>` +
      `<span class="mode"></span><span class="level"></span>`;
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

  // Board views (breadboard, pinout) built from the chip description once the first state names the chip ---------
  const viewContext = {
    pin: (name) => api.pin(name),
    cycle: cycleInput,
    forced: (name) => (forced.has(name) ? (forced.get(name) ? "high" : "low") : ""),
    setPin: (name, level) => api.setPin(name, level),
    setVoltage: (name, volts) => api.setVoltage(name, volts),
  };
  let views = null;

  function renderViews(s) {
    if (!views) {
      views = [];
      const chip = window.SiliconForgeChips?.[s.mcu];
      for (const name of ["breadboard", "pinout"]) {
        const container = $(name);
        if (chip && window.SiliconForgeViews?.[name]) views.push(window.SiliconForgeViews[name](container, chip, viewContext));
        else container.querySelector(".hint").textContent = `No ${name} view for ${s.mcu} yet: see the Pins tab.`;
      }
    }
    for (const view of views) view.update(s);
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
      const order = (pin) => Number(pin.label.slice(1));
      pins.filter((p) => p.label.startsWith("D")).sort((a, b) => order(a) - order(b)).forEach((p) => digital.append(pinElement(p, false)));
      pins.filter((p) => p.label.startsWith("A")).sort((a, b) => order(a) - order(b)).forEach((p) => analog.append(pinElement(p, true)));
    }
    for (const pin of pins) {
      const element = document.querySelector(`.pin[data-pin="${pin.name}"]`);
      if (!element) continue;
      const pwm = pin.output && pin.edges >= 2;  // Several edges in one frame: show the average level
      element.dataset.state = pin.state;
      element.dataset.output = pin.output;
      element.dataset.pwm = pwm;
      element.style.setProperty("--duty", `${(pin.duty * 100).toFixed(1)}%`);
      element.dataset.forced = forced.has(pin.name) ? (forced.get(pin.name) ? "high" : "low") : "";
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
    $("firmware-file").addEventListener("change", async (event) => {
      const file = event.target.files[0];
      if (!file) return;
      try {
        await api.load(file.name, new Uint8Array(await file.arrayBuffer()));
        await api.run();
      } catch (error) {
        alert(`Cannot load ${file.name}: ${error.message}`);
      }
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
      if (event.target.closest("input, select, textarea")) return;
      if (event.key === " ") { event.preventDefault(); $("run").click(); }
      else if (event.key === "s") api.step();
      else if (event.key === "r") api.reset();
    });
  }

  bindControls();
  webFront.ready.then(() => {
    $("status").textContent = "connected";
  }, (error) => {
    $("status").textContent = `disconnected: ${error.message ?? error}`;
    $("status").dataset.state = "stopped";
  });
})();
