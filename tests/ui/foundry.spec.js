// The PWM correction loop of docs/FOUNDRY.md (#61) in the native window: the agent, in the terminal, changes the
// firmware for the setpoint, builds it with the chain of the project and runs the bench, which finds a deviation on D6;
// the person finds the change in the editor and the failed run in the Bench panel, with the measurement and the trace;
// the agent corrects the firmware, and the person reruns the scenario from the window and gets the report of the agent.
// The shell of the terminal stands for the agent: it applies the two changes of the agent (tests/foundry/*.patch, in
// $FOUNDRY_PATCHES), as a client does through its tools. The test runs in the project made by FoundryProject.cmake; the
// cases share it: run them in the order written. The delay of the loop is kept in .siliconforge/foundry-delay.txt.
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 120000;

describe("The PWM correction loop in the window", () => {
  const sf = () => window.siliconForge;
  const terminal = () => sf().terminal;
  const editor = () => sf().editor;
  const bench = () => sf().bench;
  const firmware = "firmware/src/pwm_setpoint.c";
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const screen = () => {
    const buffer = terminal().terminal.buffer.active;
    return Array.from({length: buffer.length}, (_, row) => buffer.getLine(row).translateToString(true)).join("\n");
  };
  const waitForScreen = async (text, ms = 90000) => {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      if (screen().includes(text)) return;
      await sleep(50);
    }
    throw new Error(`"${text}" not on the screen:\n${screen()}`);
  };
  const type = (text) => terminal().terminal.input(text, true);
  /** What the agent does after each change: build with the chain of the project, run the bench, say its exit code. */
  const agent = (step, patch) => type(`git apply "$FOUNDRY_PATCHES/${patch}" && make -s -C firmware prebuilt && `
    + `siliconforge bench run scenarios/pwm-setpoint.json; printf '${step}-exit-%s\\n' $?\r`);
  const measurement = (report, id) => report.measurements.find((entry) => entry.id === id);
  let started = 0;

  beforeAll(async () => {
    await webFront.ready;
    await terminal().ready;
    await bench().ready;
    document.getElementById("terminal-toggle").click();
    await terminal().open("shell");
    document.getElementById("editor-toggle").click();
  });

  it("the agent builds its change for the setpoint and the bench finds a deviation on the pin", async () => {
    started = performance.now();
    agent("first", "1-setpoint-off.patch");
    await waitForScreen("first-exit-1");
    expect(screen()).toContain("FAIL duty-released dutyCycle on D6");
    // Started in the terminal of the window, the command handed the run to the window (#80): the person sees the
    // firmware the agent just tested running on the board.
    // The terminal is narrow: the line wraps on the screen.
    expect(screen().replaceAll("\n", "")).toContain("Window: firmware/prebuilt/pwm_setpoint.hex loaded and running");
    await sf().waitFor(() => sf().state?.firmware === "pwm_setpoint.hex" && sf().state.running, 10000);
  });

  it("the person finds the change of the agent in the editor", async () => {
    await editor().refresh();
    expect(editor().files.find((file) => file.path === firmware)?.status).toBe("M");
    await editor().open(firmware);
    await editor().view("diff");
    const added = [...document.querySelectorAll("#editor [data-view=diff] .add")].map((row) => row.textContent.trimEnd());
    expect(added).toContain("+#define SETPOINT_RELEASED_PERCENT 0");
  });

  it("the person opens the failed run: the setpoint, the measurement, the verdict and the trace", async () => {
    const {runs} = await bench().list();
    expect(runs.map((run) => `${run.name}:${run.verdict}`)).toContain("pwm-setpoint:fail");
    const {report} = await bench().open("pwm-setpoint");
    expect(report.verdict).toBe("fail");
    const released = measurement(report, "duty-released");
    expect(released.verdict).toBe("fail");
    expect(released.expected).toEqual({min: 0, max: 0.1});
    // One pulse of a timer count per period at OCR0A = 0: one cycle out of 256, as on the hardware.
    expect(released.value).toBe(0.390625);
    expect(measurement(report, "duty-released-again").value).toBe(0.390625);
    expect(measurement(report, "duty-held").verdict).toBe("pass");
    expect(bench().trace()).toContain("D6_PD6");
  });

  it("the agent corrects the firmware and the bench passes", async () => {
    agent("second", "2-disconnect-oc0a.patch");
    await waitForScreen("second-exit-0");
    expect(screen()).toContain("PWM setpoint on D6: pass");
  });

  it("the person reruns the scenario from the window and gets the report of the agent", async () => {
    const result = await bench().run("pwm-setpoint.json");
    expect(result.exitCode).toBe(0);
    expect(result.report.verdict).toBe("pass");
    expect(result.previous.verdict).toBe("pass");
    expect(result.identical).toBe(true);
    expect(measurement(result.report, "duty-released").value).toBe(0);
    const delay = (performance.now() - started) / 1000;
    type(`printf 'foundry-delay-seconds %s\\n' ${delay.toFixed(1)} > .siliconforge/foundry-delay.txt; printf 'kept-%s\\n' $?\r`);
    await waitForScreen("kept-0", 10000);
  });
});
