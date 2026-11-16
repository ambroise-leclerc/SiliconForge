// The instruments in the real window, on the demonstration circuit running firmware/src/demo.c: the red LED on D13
// toggles every 0.5 s and Timer0 drives the green LED on D6 with a Fast PWM of 16 MHz / 8 / 256 = 7812.5 Hz.
//
// The capture is not sampled: every edge carries the clock cycle it happened on, so the measurements are exact
// whatever the emulation speed, and the tolerances below are those of arithmetic, not of timing.
jasmine.getEnv().configure({random: false});

describe("Instruments", () => {
  const sf = () => window.siliconForge;
  const scope = () => sf().instruments;
  const simulated = (state) => state.cycles / state.clockHz;

  beforeAll(async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P" && state.firmware === "demo.hex", 5000);
    await sf().pause();
    await sf().reset();
    await sf().setSpeed(0);
    await sf().run();
    await sf().waitFor((state) => simulated(state) > 2.6, 10000);
    await sf().pause();
    await sf().waitFor((state) => !state.running, 2000);
    await scope().sync();
  });

  it("measures the blink of D13 to the cycle, including what the firmware loses to its own code", () => {
    // demo.c waits 50 x _delay_ms(10) between toggles and computes the PWM ramp in between: the half-period is 0.5 s
    // plus that code, about 0.06 % more. A cycle-exact instrument shows it; every half-period is the same to within
    // the few cycles by which the branches of the loop differ.
    const blink = scope().measure("D13");
    expect(blink.period).toBeGreaterThan(1.0);
    expect(blink.period).toBeLessThan(1.002);
    expect(blink.frequency).toBeCloseTo(1 / blink.period, 9);
    expect(blink.duty).toBeCloseTo(0.5, 4);
    const {t, s} = scope().samples("D13");
    const edges = t.filter((time, i) => i > 0 && s[i] !== s[i - 1]);
    expect(edges.length).toBeGreaterThanOrEqual(5);
    const halves = edges.slice(1).map((time, i) => Math.round((time - edges[i]) * sf().state.clockHz));
    for (const half of halves) expect(Math.abs(half - halves[0])).toBeLessThan(200, `half-periods in cycles: ${halves.join(", ")}`);
  });

  it("measures the PWM frequency of D6 and a duty cycle that follows the firmware", () => {
    // Around t = 1 s the firmware is at the top of its breathing ramp (OCR0A = 247): duty (247 + 1) / 256.
    const top = scope().measure("D6", 0.990, 0.999);
    expect(top.frequency).toBeCloseTo(7812.5, 0);
    expect(top.duty).toBeCloseTo(248 / 256, 2);
    expect(top.vavg).toBeCloseTo(5 * 248 / 256, 1);
    expect(top.vmax - top.vmin).toBe(5);
    // A quarter of the ramp earlier (step 25, OCR0A = 62): about a quarter of the brightness.
    const quarter = scope().measure("D6", 0.251, 0.259);
    expect(quarter.frequency).toBeCloseTo(7812.5, 0);
    expect(quarter.duty).toBeCloseTo(63 / 256, 2);
  });

  it("shows the pins of the circuit and draws their traces", async () => {
    const names = scope().shown().map((index) => sf().state.pins[index].label);
    for (const name of ["D13", "D6", "D2", "A0"]) expect(names).toContain(name);
    scope().set({mode: "logic", live: false, right: 1.0, span: 0.002, cursors: [null, null]});
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const canvas = document.querySelector("#instruments canvas");
    expect(canvas.width).toBeGreaterThan(300);
    const pixels = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
    const colours = new Set();
    for (let i = 0; i < pixels.length; i += 4 * 7) colours.add((pixels[i] << 16) | (pixels[i + 1] << 8) | pixels[i + 2]);
    expect(colours.size).toBeGreaterThan(8);  // Background, grid, labels and several traces
    expect(document.querySelector("#instruments [data-readout=timebase]").textContent).toBe("200 µs/div");
  });

  it("triggers on an edge, live, and holds a single capture", async () => {
    const d6 = sf().state.pins.findIndex((pin) => pin.label === "D6");
    scope().set({live: true, span: 0.001, trigger: {source: d6, rising: true, single: true}, triggered: null});
    await sf().setSpeed(1);
    await sf().run();
    await sf().waitFor(() => scope().view.triggered !== null && !scope().view.live, 5000);
    await sf().pause();
    const {t, s} = scope().samples("D6");
    const at = t.indexOf(scope().view.triggered);
    expect(at).toBeGreaterThan(0);  // The trigger instant is a recorded edge...
    expect(s[at]).toBe(1);          // ...a rising one
    expect(s[at - 1]).toBe(0);
    expect(document.querySelector("#instruments [data-action=live]").textContent).toBe("Held");
    scope().set({live: true, trigger: {source: -1, rising: true, single: false}, triggered: null});
  });

  it("measures an interval between two cursors placed with the mouse", async () => {
    scope().set({mode: "scope", live: false, right: 1.0, span: 0.001, cursors: [null, null]});
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const canvas = document.querySelector("#instruments canvas");
    const bounds = canvas.getBoundingClientRect();
    const click = (x, shiftKey) => {
      for (const type of ["pointerdown", "pointerup"])
        canvas.dispatchEvent(new PointerEvent(type, {clientX: bounds.left + x, clientY: bounds.top + 40, shiftKey, bubbles: true}));
    };
    click(200, false);
    click(300, true);
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const [a, b] = scope().view.cursors;
    expect(b).toBeGreaterThan(a);
    expect(document.querySelector("#instruments [data-readout=cursors]").textContent).toMatch(/^Δt \d+(\.\d+)? µs {3}1\/Δt \d+(\.\d+)? kHz$/);
  });

  it("exports the capture as a VCD file with one time unit per clock cycle", () => {
    const d13 = sf().state.pins.findIndex((pin) => pin.label === "D13");
    const text = scope().vcd([d13]);
    expect(text).toContain("$timescale 62500 ps $end");
    expect(text).toContain("$var wire 1 ! D13 $end");
    const changes = text.split("\n").filter((line) => line === "0!" || line === "1!");
    const {t, s} = scope().samples("D13");
    expect(changes.length).toBe(t.filter((time, i) => i === 0 || s[i] !== s[i - 1]).length);
    // Times are clock cycles: the first fall of D13 is the recorded edge, to the cycle.
    const fall = Math.round(t[s.findIndex((state, i) => i > 0 && state === 0 && s[i - 1] === 1)] * sf().state.clockHz);
    expect(fall).toBeGreaterThan(8000000);
    expect(text).toContain(`\n#${fall}\n0!\n`);
  });

  it("restarts the capture with the firmware", async () => {
    await sf().reset();
    await scope().sync();
    const {t} = scope().samples("D13");
    expect(t.length).toBeLessThan(4);
    expect(t.every((time) => time < 0.01)).toBe(true);
  });
});
