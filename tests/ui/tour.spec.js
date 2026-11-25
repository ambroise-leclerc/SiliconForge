// A guided tour of the interface, captured from the real window: the images of the README and of the pull requests.
// Each step also asserts what the image is meant to show, so a tour that no longer shows it fails instead of
// publishing a misleading picture. Images: build/tests/webfront-jasmine/UI-Tour/captures/*.png.
jasmine.getEnv().configure({random: false});

describe("Tour of the interface", () => {
  const sf = () => window.siliconForge;
  const simulated = (state) => state.cycles / state.clockHz;
  const settle = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  beforeAll(async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 20000);
    document.documentElement.dataset.theme = "dark";
  }, 30000);

  it("shows the demonstration circuit with the logic analyser triggered on the PWM", async () => {
    await sf().loadExample("demo");
    await sf().setSpeed(1);
    await sf().waitFor((state) => simulated(state) > 1.2 && document.querySelector('.bb-led[data-pin="D6"]') !== null, 8000);
    sf().instruments.set({mode: "logic", span: 0.001, trigger: {source: sf().state.pins.findIndex((pin) => pin.label === "D6"), rising: true, single: false}});
    await sf().waitFor(() => sf().instruments.view.triggered !== null, 3000);
    // The green LED breathes with a 2 s period: take the picture while it is clearly lit, whenever the tour starts.
    const green = () => Number(document.querySelector('.bb-led[data-pin="D6"]').dataset.level);
    await sf().waitFor(() => green() > 0.3 && green() < 0.8, 4000);
    expect(sf().instruments.measure("D6", sf().instruments.now - 0.005).frequency).toBeCloseTo(7812.5, 0);
    await webFrontCapture.screenshot("demo-logic");
    // The animation of the README (tools/captures2gif): four seconds of the LEDs and of the analyser, live.
    const frames = await webFrontCapture.record("demo", {seconds: 4, framesPerSecond: 10});
    expect(frames).toBeGreaterThan(2);  // Paced by the engine: a loaded machine records fewer images
  }, 40000);

  it("shows the same signals as voltages on the oscilloscope", async () => {
    await sf().setComponent("pot-a0", "position", 0.66);
    sf().instruments.set({mode: "scope", span: 0.002});
    await sf().waitFor(() => Math.abs(sf().pin("A0").volts - 3.3) < 0.01, 3000);
    await settle(300);
    await webFrontCapture.screenshot("demo-scope");
  }, 30000);

  it("shows the 7-segment counter with its serial frames decoded", async () => {
    await sf().loadExample("counter");
    await sf().waitFor(() => document.querySelector(".bb-seven-segment") !== null && sf().output.includes("3\r\n"), 8000);
    const tx = sf().state.pins.findIndex((pin) => pin.label === "D1");
    await sf().instruments.sync();
    // Hold the display on the last complete message: the frames of a digit, CR and LF, decoded under the trace.
    const {t, s} = sf().instruments.samples("D1");
    let start = t.length - 1;
    while (start > 0 && !(s[start] === 0 && t[start] - t[start - 1] > 0.1)) --start;
    sf().instruments.set({mode: "logic", span: 0.004, live: false, right: t[start] + 0.0036, cursors: [t[start], t[start] + 10 / 9615.4],
                          trigger: {source: -1, rising: true, single: false}, triggered: null, uart: {source: tx, baud: 0}});
    await settle(300);
    expect(sf().instruments.decodeUart("D1", 0, t[start], t[start] + 0.0036).text).toMatch(/^[0-9]\r\n$/);
    expect(document.querySelector(".bb-seven-segment").dataset.digit).toMatch(/^[0-9]$/);
    await webFrontCapture.screenshot("counter");
  }, 30000);

  it("shows the SPI display example with the analyser on the display bus", async () => {
    await sf().loadExample("tft_spi");
    await sf().waitFor((state) => state.components?.tft?.pixelsWritten === 9600, 20000);
    await sf().waitFor(() => document.querySelector('#breadboard .bb-tft-panel image[visibility="visible"]') !== null, 10000);
    expect(document.querySelectorAll("#breadboard .bb-tft-pad").length).toBe(9);
    await settle(500);
    await webFrontCapture.screenshot("tft-spi");
  }, 40000);

  it("shows the supply, the load and the multimeter side by side", async () => {
    await sf().selectCircuit("bench-demo");
    await sf().waitFor(() => sf().devices?.faceplate("psu") && sf().devices.faceplate("load") && sf().devices.faceplate("dmm"), 20000);
    await sf().setComponent("load", "resistance", 100);
    await sf().setComponent("psu", "voltage", 5);
    await sf().setComponent("psu", "currentLimit", 0.1);
    await sf().setComponent("psu", "enabled", 1);
    sf().devices.show(["supply", "load", "multimeter"]);
    await sf().waitFor(() => sf().component("psu")?.mode === 1 && Math.abs(sf().component("psu").outputCurrent - 0.05) < 1e-4, 5000);
    await settle(500);
    await webFrontCapture.screenshot("bench-devices");
    sf().devices.show(["logic"]);
  }, 40000);

  it("has a light theme and a pinout view", async () => {
    document.documentElement.dataset.theme = "light";
    document.getElementById("tab-pinout").click();
    await settle(300);
    await webFrontCapture.screenshot("light-pinout");
    document.getElementById("tab-breadboard").click();
    document.documentElement.dataset.theme = "dark";
  });
});
