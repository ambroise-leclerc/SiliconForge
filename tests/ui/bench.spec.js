// The bench in the native window (#60): a scenario of the project runs with the engine of siliconforge-bench, its
// evidence opens with the setpoint, the measurement, the verdict and the trace, and a second run is compared with the
// first. The test runs in a project directory made by tests/CMakeLists.txt: scenarios/pwm-setpoint.json, and
// scenarios/off-target.json, the same scenario with a measurement that cannot hold. The runs it makes stay in that
// directory, where siliconforge-bench would keep them.
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 60000;

describe("The bench in the window", () => {
  const bench = () => window.siliconForge.bench;
  const panel = () => document.getElementById("bench");
  const readout = (name) => panel().querySelector(`[data-readout=${name}]`);
  const rows = (list) => [...panel().querySelectorAll(`[data-list=${list}] tr`)];

  it("lists the scenarios of the project", async () => {
    await bench().ready;
    const {scenarios} = await bench().list();
    expect(scenarios).toEqual(["off-target.json", "pwm-setpoint.json"]);
    const options = [...panel().querySelectorAll("[data-select=scenario] option")].map((option) => option.value);
    expect(options).toEqual(scenarios);
  });

  it("runs a scenario from the window and shows its verdict, measurements and steps", async () => {
    const result = await bench().run("pwm-setpoint.json");
    expect(result.exitCode).toBe(0);
    expect(result.report.verdict).toBe("pass");
    expect(readout("verdict").textContent).toBe("pass");
    expect(readout("title").textContent).toContain("PWM setpoint on D6");
    expect(rows("measurements").length).toBe(4);
    expect(rows("measurements")[0].textContent).toContain("duty-released");
    expect(rows("steps").map((row) => row.dataset.outcome)).toEqual(Array(9).fill("done"));
    expect(rows("steps")[3].textContent).toContain("button-d2.pressed = 1");
    expect(readout("trace").textContent).toContain("trace.vcd");
    expect(panel().querySelector("[data-action=save-trace]").disabled).toBe(false);
  });

  it("compares a second run with the first: same inputs, same report", async () => {
    const first = (await bench().run("pwm-setpoint.json")).report;
    const again = await bench().run("pwm-setpoint.json");
    expect(again.identical).toBe(true);
    expect(again.previous).toEqual(first);
    expect(bench().differences(again.previous, again.report)).toEqual([]);
    expect(readout("comparison").dataset.state).toBe("same");
    expect(readout("comparison").textContent).toContain("Same report");
  });

  it("reports the differences between two runs path by path", () => {
    const before = {verdict: "pass", measurements: [{value: 25}], inputs: [{sha256: "a"}]};
    const after = {verdict: "fail", measurements: [{value: 25}], inputs: [{sha256: "b"}]};
    expect(bench().differences(before, after)).toEqual([
      {path: "/verdict", before: "pass", after: "fail"},
      {path: "/inputs/0/sha256", before: "a", after: "b"},
    ]);
  });

  it("lists every difference between two runs, not a sample", () => {
    const measurements = (value) => Array.from({length: 9}, (_, index) => ({id: `m${index}`, value: value + index}));
    const report = (value) => ({scenario: "s", verdict: "pass", inputs: [], steps: [], serial: [], measurements: measurements(value)});
    bench().show({run: "s", directory: "d", report: report(2), trace: null, previous: report(1), identical: false});
    expect(readout("comparison").textContent).toContain("9 place(s)");
    const details = panel().querySelector("[data-section=changes]");
    expect(details.hidden).toBe(false);
    const paths = [...details.querySelectorAll("li code")].map((code) => code.textContent);
    expect(paths.length).toBe(9);
    expect(paths[8]).toBe("/measurements/8/value");
    bench().show({run: "s", directory: "d", report: report(1), trace: null, previous: report(1), identical: true});
    expect(details.hidden).toBe(true);
  });

  it("opens a failed run and finds the stimulus, the measure, the expectation, the verdict and the trace", async () => {
    await bench().run("off-target.json");
    const opened = await bench().open("off-target");
    expect(opened.report.verdict).toBe("fail");
    expect(readout("verdict").dataset.verdict).toBe("fail");
    const failed = rows("measurements").filter((row) => row.dataset.verdict === "fail");
    expect(failed.length).toBe(1);
    expect(failed[0].textContent).toContain("duty-released");
    expect(failed[0].textContent).toContain("49.5 to 50.5 %");
    expect(rows("steps")[3].textContent).toContain("button-d2.pressed = 1");
    expect(bench().trace().startsWith("$")).toBe(true);
    const {runs} = await bench().list();
    expect(runs.map((run) => `${run.name}:${run.verdict}`)).toEqual(["off-target:fail", "pwm-setpoint:pass"]);
  });

  it("refuses a scenario or a run that is not in the project, with a typed error", async () => {
    const code = async (promise) => { try { await promise; } catch (error) { return error.code; } return null; };
    expect(await code(bench().run("../pwm-setpoint.json"))).toBe("siliconforge.unknownScenario");
    expect(await code(bench().open("../scenarios"))).toBe("siliconforge.unknownRun");
  });
});
