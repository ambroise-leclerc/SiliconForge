// The signal generator in the real window, on the "generator-demo" circuit (#63, ADR 0010): the faceplate programs a wave
// through the electrical model, shows what it holds, publishes what it can do, and a request beyond the capacity of the
// clock is refused with the faceplate showing the value that stands.
jasmine.getEnv().configure({random: false});

describe("Signal generator", () => {
  const sf = () => window.siliconForge;
  const devices = () => sf().devices;
  const until = async (predicate, what, timeoutMs = 10000) => {
    const deadline = performance.now() + timeoutMs;
    while (!predicate()) {
      if (performance.now() > deadline) throw new Error(`timeout waiting for ${what}`);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  };
  const plate = () => devices().faceplate("gbf");
  const change = (input, value) => {
    input.value = String(value);
    input.dispatchEvent(new Event("change", {bubbles: true}));
  };

  beforeAll(async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 20000);
    await until(() => devices() && plate(), "the faceplate of the generator", 20000);
  }, 40000);

  it("is a device of the bar, with its jacks and the markers of its leads on the breadboard", () => {
    document.querySelector("[data-device=generator]").click();
    expect(devices().visible).toEqual(["generator"]);
    expect(document.querySelector("[data-device-group=generator]").hidden).toBe(false);
    expect([...plate().querySelectorAll(".jack")].map((jack) => jack.className.replace("jack ", ""))).toEqual(["volt", "com"]);
    expect([...document.querySelectorAll("[data-id=gbf] .bb-probe")].map((probe) => probe.getAttribute("class").replace("bb-probe ", ""))).toEqual(["volt", "com"]);
    expect(plate().querySelector(".jack.volt").textContent).toContain("i24");
  });

  it("publishes its bounds and its time resolution with the circuit", async () => {
    const limits = (await sf().circuit()).components.find((component) => component.id === "gbf").limits;
    expect(limits.voltage).toEqual({min: -20, max: 20, unit: "V"});
    expect(limits.timeResolution.cycles).toBe(1);
    expect(limits.maxFrequency.sine).toBeCloseTo(15625, 6);
    expect(plate().querySelector(".hint").textContent).toContain("15625");
  });

  it("programs a square wave and enables the channel", async () => {
    change(plate().querySelector("[data-set=waveform]"), 1);
    change(plate().querySelector("[data-set=frequency]"), 50);
    change(plate().querySelector("[data-set=amplitude]"), 1.5);
    change(plate().querySelector("[data-set=offset]"), 2.5);
    plate().querySelector(".output").click();
    await until(() => sf().component("gbf")?.mode === 1, "the output held");
    // The window samples the wave at its own pace: it is high (4 V) or low (1 V), nothing else.
    const seen = new Set();
    for (let i = 0; i < 40; ++i) {
      seen.add(Number(sf().component("gbf").outputVoltage.toFixed(2)));
      await new Promise((resolve) => setTimeout(resolve, 15));
    }
    expect([...seen].every((volts) => volts === 4 || volts === 1)).toBe(true);
    expect(plate().dataset.wave).toBe("square");
  });

  it("refuses a frequency the clock cannot step and shows the value that stands", async () => {
    change(plate().querySelector("[data-set=waveform]"), 0);
    await until(() => sf().component("gbf")?.waveform === 0, "the sine");
    const field = plate().querySelector("[data-set=frequency]");
    change(field, 20000);  // Within the 100 kHz of the type, beyond what a sine steps at 16 MHz.
    await new Promise((resolve) => setTimeout(resolve, 300));
    expect(sf().component("gbf").frequency).toBe(50);
    await until(() => field.value === "50", "the field back to the frequency that stands");
  });

  it("shows the generator beside the other devices", async () => {
    await webFrontCapture.screenshot("bench-generator");
  });
});
