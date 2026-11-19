// The agent terminal in the native window (#55): a program runs in a native pseudoterminal in the project directory,
// takes typed input, follows the size of the panel, is interrupted by Ctrl+C, and the bench stays usable once it is
// closed. The shell stands for a client: the same session carries Claude Code, Codex, OpenCode and Mistral Vibe.
// The test runs in the project directory made by tests/CMakeLists.txt (scenarios/pwm-setpoint.json...).
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 30000;

describe("The agent terminal in the window", () => {
  const sf = () => window.siliconForge;
  const terminal = () => sf().terminal;
  const panel = () => document.getElementById("terminal");
  const status = () => panel().querySelector("[data-readout=status]");
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  /** The text of the screen and its scrollback. */
  const screen = () => {
    const buffer = terminal().terminal.buffer.active;
    return Array.from({length: buffer.length}, (_, row) => buffer.getLine(row).translateToString(true)).join("\n");
  };
  const waitForScreen = async (text, ms = 15000) => {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      if (screen().includes(text)) return;
      await sleep(50);
    }
    throw new Error(`"${text}" not on the screen:\n${screen()}`);
  };
  /** Types as the user does: through xterm.js, so that the adapter carries it to the pseudoterminal. */
  const type = (text) => terminal().terminal.input(text, true);

  beforeAll(async () => {
    await webFront.ready;
    await terminal().ready;
  });

  it("offers the shell and the clients installed on this machine", () => {
    expect(terminal().enabled).toBe(true);
    const shell = terminal().clients.find((client) => client.id === "shell");
    expect(shell.available).toBe(true);
    expect(terminal().clients.map((client) => client.id)).toEqual(["claude", "codex", "opencode", "vibe", "shell"]);
    const options = [...panel().querySelectorAll("[data-select=client] option")].map((option) => option.value);
    expect(options).toEqual(["claude", "codex", "opencode", "vibe", "shell"]);
  });

  it("opens a session in the project directory and carries the typed input", async () => {
    document.getElementById("terminal-toggle").click();
    expect(panel().hidden).toBe(false);
    await terminal().open("shell");
    expect(status().textContent).toBe("running");
    // The printf format splits the result from the command that is echoed back, so only the execution can match.
    type("printf 'sf-%s\\n' 42\r");
    await waitForScreen("sf-42");
    type("ls scenarios\r");
    await waitForScreen("pwm-setpoint.json");
  });

  it("does not hand the client the session of the IDE or the agent the application was started from", async () => {
    // The test is started with those variables set. Reaching the IDE of another session made the embedded client take
    // it from the first one; the settings of the user stay.
    type("printf 'host-%s-%s-%s\\n' \"${CLAUDE_CODE_SSE_PORT:-none}\" \"${TERM_PROGRAM:-none}\" \"${CLAUDE_CODE_USE_BEDROCK:-none}\"\r");
    await waitForScreen("host-none-none-1");
  });

  it("gives the program the size of the panel and follows its changes", async () => {
    const size = () => `${terminal().terminal.rows} ${terminal().terminal.cols}`;
    type("stty size\r");
    await waitForScreen(size());
    panel().style.height = "220px";
    const before = size();
    const end = Date.now() + 10000;
    while (size() === before && Date.now() < end) await sleep(50);
    expect(size()).not.toBe(before);
    await sleep(300);  // The new size reaches the pseudoterminal through the bridge.
    type("stty size; printf 'size-%s\\n' done\r");
    await waitForScreen("size-done");
    expect(screen().split("\n").filter((line) => line === size()).length).toBeGreaterThanOrEqual(1);
  });

  it("interrupts the program in the foreground with Ctrl+C", async () => {
    type("sleep 30\r");
    await sleep(500);
    type("\x03");
    type("printf 'after-%s\\n' 2\r");
    await waitForScreen("after-2", 10000);  // Far sooner than the 30 seconds of the sleep.
  });

  it("keeps the session when another client is shown or the page attaches again", async () => {
    type("printf 'kept-%s\\n' 1\r");
    await waitForScreen("kept-1");
    await terminal().open("shell");  // Attaches to the session that runs, replaying what it printed.
    await waitForScreen("kept-1");
    expect(terminal().state.running).toBe(true);
  });

  it("keeps the panel coherent when openings follow each other quickly", async () => {
    const screens = () => panel().querySelectorAll("[data-view=screen] .xterm").length;
    // The first opening asks to restart a session that still runs: the application refuses it. The second one replaces
    // it before it settles, so its error must neither show nor change the status of the client now attached.
    const refused = terminal().open("shell", true);
    const attached = terminal().open("shell");
    expect(await refused).toBeNull();
    expect(await attached).toBe(terminal());
    expect(panel().querySelector("[data-readout=error]").hidden).toBe(true);
    expect(terminal().state).toEqual({client: "shell", running: true, exitCode: null});
    expect(screens()).toBe(1);
    // Three openings at once leave one terminal on the screen, and it is the one that takes the input.
    await Promise.all([terminal().open("shell"), terminal().open("shell"), terminal().open("shell")]);
    expect(screens()).toBe(1);
    type("printf 'quick-%s\\n' 1\r");
    await waitForScreen("quick-1");
  });

  it("ends the client on request and leaves the bench usable", async () => {
    await terminal().hangUp();
    const end = Date.now() + 10000;
    while (terminal().state.running && Date.now() < end) await sleep(50);
    expect(terminal().state.running).toBe(false);
    expect(status().dataset.state).toBe("exited");
    expect((await sf().bench.list()).scenarios).toContain("pwm-setpoint.json");
    const result = await sf().bench.run("pwm-setpoint.json");
    expect(result.report.verdict).toBe("pass");
  });

  it("starts a new session after the client exited", async () => {
    await terminal().open("shell", true);
    expect(status().textContent).toBe("running");
    type("printf 'again-%s\\n' 1\r");
    await waitForScreen("again-1");
    type("exit\r");
    const end = Date.now() + 10000;
    while (terminal().state.running && Date.now() < end) await sleep(50);
    expect(terminal().state).toEqual({client: "shell", running: false, exitCode: 0});
    expect(status().textContent).toBe("exited (0)");
  });
});
