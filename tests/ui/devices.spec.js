// The bench devices in the real window, on the "bench-demo" circuit (ADR 0010): a programmable supply feeding a
// variable load, with a multimeter across the load. The faceplates set the components through the model and show what
// it computes; the breadboard marks, with the symbol and the colour of the jacks, the holes where each lead is connected.
jasmine.getEnv().configure({random: false});

describe("Bench devices", () => {
  const sf = () => window.siliconForge;
  const devices = () => sf().devices;
  const until = async (predicate, what, timeoutMs = 10000) => {
    const deadline = performance.now() + timeoutMs;
    while (!predicate()) {
      if (performance.now() > deadline) throw new Error(`timeout waiting for ${what}`);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  };
  const plate = (id) => devices().faceplate(id);
  const read = (id, name) => plate(id).querySelector(`[data-read=${name}]`).textContent;
  const change = (input, value) => {
    input.value = String(value);
    input.dispatchEvent(new Event("change", {bubbles: true}));
  };
  const hasDevice = (name) => !document.querySelector(`[data-device-group=${name}]`).hidden;

  beforeAll(async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 20000);
    await until(() => devices() && plate("psu") && plate("load") && plate("dmm"), "the faceplates of the circuit", 20000);
  }, 40000);

  it("starts on the logic analyser and switches from one device to another", () => {
    expect(devices().visible).toEqual(["logic"]);
    expect(hasDevice("analyser")).toBe(true);
    expect(hasDevice("supply")).toBe(false);
    document.querySelector("[data-device=supply]").click();
    expect(devices().visible).toEqual(["supply"]);
    expect(hasDevice("analyser")).toBe(false);
    expect(hasDevice("supply")).toBe(true);
    expect(document.querySelector("[data-device=supply]").getAttribute("aria-pressed")).toBe("true");
  });

  it("shows several devices at once with Shift-click or the side-by-side switch", () => {
    document.querySelector("[data-device=load]").dispatchEvent(new MouseEvent("click", {bubbles: true, shiftKey: true}));
    expect(devices().visible).toEqual(["supply", "load"]);
    document.querySelector("[data-device=multimeter]").dispatchEvent(new MouseEvent("click", {bubbles: true, shiftKey: true}));
    expect(devices().visible).toEqual(["supply", "load", "multimeter"]);
    // The analyser and the oscilloscope are two faces of one capture: they exclude each other.
    document.querySelector("[data-device=logic]").dispatchEvent(new MouseEvent("click", {bubbles: true, shiftKey: true}));
    document.querySelector("[data-device=scope]").dispatchEvent(new MouseEvent("click", {bubbles: true, shiftKey: true}));
    expect(devices().visible).toEqual(["supply", "load", "multimeter", "scope"]);
    expect(sf().instruments.view.mode).toBe("scope");
    devices().show(["supply", "load", "multimeter"]);
  });

  it("sets the supply from its faceplate and reads the voltage and the current at the node", async () => {
    change(plate("load").querySelector("[data-set=resistance]"), 100);
    change(plate("psu").querySelector("[data-set=voltage]"), 5);
    change(plate("psu").querySelector("[data-set=currentLimit]"), 100);  // mA
    plate("psu").querySelector(".output").click();
    await until(() => sf().component("psu")?.mode === 1, "constant voltage");
    expect(sf().component("psu").outputVoltage).toBeCloseTo(5, 3);
    expect(sf().component("psu").outputCurrent).toBeCloseTo(0.05, 4);
    await until(() => read("psu", "v") === "5.000 V" && read("psu", "i") === "50.0 mA", "the displays of the supply");
    await until(() => read("dmm", "v") === "5.000 V", "the multimeter");
    expect(plate("psu").dataset.supplyMode).toBe("cv");
  });

  it("limits the current and lets the voltage fall when the load drops", async () => {
    change(plate("load").querySelector("[data-set=resistance]"), 25);
    await until(() => sf().component("psu")?.mode === 2, "constant current");
    await until(() => read("psu", "i") === "100.0 mA" && read("psu", "v") === "2.500 V" && read("dmm", "v") === "2.500 V", "the limited readings");
    expect(plate("psu").dataset.supplyMode).toBe("cc");
    expect(read("load", "p")).toBe("250.0 mW");
  });

  it("shows the clamped value in a setpoint field that keeps the focus", async () => {
    const field = plate("psu").querySelector("[data-set=voltage]");
    field.focus();
    change(field, 40);  // beyond the 30 V of the channel
    expect(field.value).toBe("30");
    await until(() => sf().component("psu")?.voltage === 30, "the clamped setpoint");
    change(field, 5);
    await until(() => sf().component("psu")?.voltage === 5, "the setpoint back to 5 V");
    field.blur();
  });

  it("shows the supply, the load and the multimeter side by side with their markers on the breadboard", async () => {
    devices().show(["supply", "load", "multimeter"]);
    await new Promise((resolve) => setTimeout(resolve, 300));
    await webFrontCapture.screenshot("bench-devices");
  });

  it("marks where each instrument is connected, with the symbol and the colour of its jacks", () => {
    const kinds = (id) => [...document.querySelectorAll(`[data-id=${id}] .bb-probe`)].map((probe) => probe.getAttribute("class").replace("bb-probe ", ""));
    expect(kinds("psu")).toEqual(["plus", "minus"]);
    expect(kinds("dmm")).toEqual(["volt", "com"]);
    const jacks = (id) => [...plate(id).querySelectorAll(".jack")].map((jack) => jack.className.replace("jack ", ""));
    expect(jacks("psu")).toEqual(["plus", "minus"]);
    expect(jacks("dmm")).toEqual(["volt", "com"]);
    // Each jack names the hole of its marker.
    expect(plate("psu").querySelector(".jack.plus").textContent).toContain("a27");
    expect(plate("dmm").querySelector(".jack.com").textContent).toContain("d29");
  });

  it("marks the channels of the analyser on the legs of the chip, in the colour of their traces", async () => {
    devices().show(["logic"]);
    await until(() => document.querySelector(".bb-channel-mark") !== null, "the channel markers");
    const marks = [...document.querySelectorAll(".bb-channel-mark circle")].map((circle) => circle.getAttribute("stroke"));
    expect(marks.length).toBeGreaterThan(0);
    expect(new Set(marks).size).toBe(marks.length);
    devices().show(["supply"]);
    await until(() => document.querySelector(".bb-channel-mark") === null, "the markers to leave with the analyser");
  });
});
