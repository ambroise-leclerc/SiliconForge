// Simulated-time check of the whole demo chain: blink.hex -> AVR core -> D13 -> WebBridge -> breadboard LED (ADR 0003).
//
// The LED is observed the way a user sees it: the `data-level` attribute of `.bb-led[data-pin="D13"]`, which the
// breadboard renders from each pushed frame. That level is the fraction of the frame the pin spent high. Times are the
// simulated clock (`cycles / clockHz`), never the wall clock.
//
// Two complementary measurements, because a frame only carries averages:
//  - at x5 a frame covers ~0.17 simulated seconds, less than a half-period, so it holds at most one edge and its level
//    locates that edge precisely: every half-period is checked (0.5 s +- 1 %);
//  - at full speed (setSpeed(0)) the page receives a frame every ~2 simulated seconds on average, each holding several
//    edges, so edges cannot be placed. The pin's per-frame edge counts (`pin.edges`) are summed over the longest run of
//    consecutive frames instead: the count fixes the period to within one edge (~1.5 % over 30 s, so a period error
//    beyond that fails), and the rendered level must average 0.5. The exact 1 % per half-period is checked at x5.
describe("Blink LED in simulated time", () => {
  const sf = () => window.siliconForge;
  const led = () => document.querySelector('.bb-led[data-pin="D13"]');
  const HALF_PERIOD = 0.5;
  const TOLERANCE = 0.01;

  /** Frames received since the call, with the rendered LED level and D13's edge count. */
  const record = () => {
    const frames = [];
    let last = null;
    return {
      frames,
      check(state) {
        if (state.cycles === last) return;
        last = state.cycles;
        const element = led();
        frames.push({
          frame: state.frame,
          time: state.cycles / state.clockHz,
          level: element ? Number(element.dataset.level) : NaN,
          edges: sf().pin("D13").edges,
          high: sf().pin("D13").state === sf().pinState.high,
        });
      },
    };
  };

  const runUntil = async (seconds, wallMs) => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 5000);
    await sf().waitFor(() => led() !== null, 5000);  // The breadboard is built once the chip description arrives.
    await sf().pause();
    await sf().reset();
    await sf().waitFor((state) => !state.running && state.cycles === 0, 5000);  // No frame from before the reset
    return async (speed) => {
      await sf().setSpeed(speed);
      const recording = record();
      await sf().run();
      try {
        await sf().waitFor((state) => {
          recording.check(state);
          return state.cycles / state.clockHz > seconds;
        }, wallMs);
      } finally {
        await sf().pause();
      }
      return recording.frames;
    };
  };

  /**
   * Edges placed from the rendered levels. Walking the frames while tracking whether the pin is high at the end of the
   * previous one: a high pin that renders below 1 fell during the frame, after `level * duration`; a low pin that renders
   * above 0 rose during it, after `(1 - level) * duration`. An edge on a frame boundary is thus placed on the boundary.
   */
  const edgesOf = (frames) => {
    const edges = [];
    let high = frames[0].level >= 0.5;
    for (let i = 1; i < frames.length; i++) {
      const {level, time} = frames[i];
      const start = frames[i - 1].time;
      // A skipped frame (slow client) widens the interval the level was averaged over: the edge cannot be placed.
      const exact = frames[i].frame === frames[i - 1].frame + 1;
      if (high && level < 1) {
        edges.push({rising: false, exact, time: start + level * (time - start)});
        high = false;
      } else if (!high && level > 0) {
        edges.push({rising: true, exact, time: start + (1 - level) * (time - start)});
        high = true;
      }
    }
    return edges;
  };

  it("alternates on and off with a 0.5 s half-period (1 Hz), edge by edge at x5", async () => {
    const measure = await runUntil(4.2, 4500);
    const frames = await measure(5);
    const longest = Math.max(...frames.slice(1).map((frame, i) => frame.time - frames[i].time));
    expect(longest).toBeLessThan(HALF_PERIOD * 0.9, "a frame must not hold two edges");

    const edges = edgesOf(frames);
    expect(edges.length).toBeGreaterThanOrEqual(8);  // 4.2 s of 1 Hz blinking: more than three periods
    let measured = 0;
    for (let i = 1; i < edges.length; i++) {
      expect(edges[i].rising).toBe(!edges[i - 1].rising);  // Strict alternation: no missed or merged edge
      if (!edges[i].exact || !edges[i - 1].exact) continue;
      measured++;
      const half = edges[i].time - edges[i - 1].time;
      expect(Math.abs(half - HALF_PERIOD)).toBeLessThanOrEqual(HALF_PERIOD * TOLERANCE,
        `half-period ${i} measured ${half.toFixed(4)} s`);
    }
    expect(measured).toBeGreaterThanOrEqual(6);  // At least three periods were measured, not skipped
  });

  it("keeps a 1 Hz period at full speed", async () => {
    const measure = await runUntil(30, 4500);
    const frames = await measure(0);

    // Runs of consecutive frames: the edge counts of a skipped frame are lost, so counting never spans a gap.
    const runs = [];
    for (const frame of frames) {
      const run = runs.at(-1);
      if (run && frame.frame === run.at(-1).frame + 1) run.push(frame);
      else runs.push([frame]);
    }

    // Edges counted by the pin over the longest run, from the end of its first frame to the end of its last one. The
    // edges fall on a regular grid of step h, so N edges in a span D satisfy D / (N + 1) < h < D / (N - 1): the count
    // fixes the half-period to one edge, about 1.5 % over a 30 s run (a single frame is no help: it holds several edges).
    const best = runs.reduce((longest, run) => (run.length > longest.length ? run : longest));
    expect(best.length).toBeGreaterThan(8);
    const span = best.at(-1).time - best[0].time;
    const counted = best.slice(1).reduce((sum, frame) => sum + frame.edges, 0);
    expect(span).toBeGreaterThan(20);
    const lowest = span / (counted + 1);
    const highest = span / (counted - 1);
    // The 1 % band around the nominal half-period must overlap the interval the count allows.
    expect(lowest).toBeLessThanOrEqual(HALF_PERIOD * (1 + TOLERANCE),
      `${counted} edges in ${span.toFixed(2)} s simulated: half-period between ${lowest.toFixed(4)} and ${highest.toFixed(4)} s`);
    expect(highest).toBeGreaterThanOrEqual(HALF_PERIOD * (1 - TOLERANCE),
      `${counted} edges in ${span.toFixed(2)} s simulated: half-period between ${lowest.toFixed(4)} and ${highest.toFixed(4)} s`);

    // What the user sees: the LED averages half brightness over the run.
    const elapsed = best.at(-1).time - best[0].time;
    const lit = best.slice(1).reduce((sum, frame, i) => sum + frame.level * (frame.time - best[i].time), 0) / elapsed;
    expect(lit).toBeGreaterThan(0.48);
    expect(lit).toBeLessThan(0.52);
  });
});
