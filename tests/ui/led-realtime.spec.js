// Wall-clock check of the demo (ADR 0003): at x1 the LED of the breadboard blinks at 1 Hz as a human sees it.
//
// This guards what the simulated-time spec cannot: the pacing of `Simulation` and the frame rate of `WebBridge`
// (about 30 frames/s). Edges are timed with `performance.now()` on the frame that first shows the new level.
//
// Tolerance for loaded CI machines: the mean period must be within 10 % of 1 s, and each half-period within 0.5 s +-
// 0.15 s, which still catches a lost frame that would merge two states (1 s between edges) or a missed one. The
// measurement is attempted twice; the spec fails, with both measurements in the message, only if both attempts fail.
describe("Blink LED in real time", () => {
  const sf = () => window.siliconForge;
  const led = () => document.querySelector('.bb-led[data-pin="D13"]');
  const WINDOW_MS = 4500;  // Wall time observed after the first edge: 4 full periods plus a margin.
  const ATTEMPTS = 2;

  /** Runs the firmware from reset at x1 and returns the wall-clock times (ms) of the LED's edges, first one included. */
  const measureEdges = async () => {
    await sf().pause();
    await sf().reset();
    await sf().setSpeed(1);
    const edges = [];
    let lit = null;
    let firstEdge = null;
    const observe = (state) => {
      const element = led();
      if (!element) return false;
      const on = Number(element.dataset.level) >= 0.5;
      if (lit !== null && on !== lit) {
        const now = performance.now();
        edges.push({time: now, rising: on});
        firstEdge ??= now;
      }
      lit = on;
      return firstEdge !== null && performance.now() - firstEdge >= WINDOW_MS;
    };
    await sf().run();
    try {
      await sf().waitFor(observe, WINDOW_MS + 4000);
    } finally {
      await sf().pause();
    }
    return edges;
  };

  /** Returns a description of what is wrong with the edges, or null. */
  const problem = (edges) => {
    if (edges.length < 8) return `only ${edges.length} edges in ${WINDOW_MS} ms (expected about 9)`;
    for (let i = 1; i < edges.length; i++) {
      if (edges[i].rising === edges[i - 1].rising) return `edge ${i} repeats the previous direction (missed edge)`;
      const half = edges[i].time - edges[i - 1].time;
      if (Math.abs(half - 500) > 150) return `half-period ${i} lasted ${half.toFixed(0)} ms (expected 500 +-150)`;
    }
    const period = (edges.at(-1).time - edges[0].time) / (edges.length - 1) * 2;
    if (period < 900 || period > 1100) return `mean period ${period.toFixed(0)} ms (expected 1000 +-100)`;
    return null;
  };

  it("blinks at 1 Hz on the wall clock", async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 5000);
    await sf().waitFor(() => led() !== null, 5000);
    const failures = [];
    for (let attempt = 1; attempt <= ATTEMPTS; attempt++) {
      const issue = problem(await measureEdges());
      if (issue === null) return;
      failures.push(`attempt ${attempt}: ${issue}`);
    }
    fail(`The LED does not blink at 1 Hz in real time. ${failures.join("; ")}`);
  }, 30000);
});
