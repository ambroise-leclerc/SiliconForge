// The agent terminal in the native window (#55): a program runs in a native pseudoterminal in the project directory,
// takes typed input, follows the size of the panel, is interrupted by Ctrl+C, and the bench stays usable once it is
// closed. The shell stands for a client: the same session carries Claude Code, Codex, OpenCode and Mistral Vibe.
// The shell is sh on Linux and macOS and cmd.exe (ConPTY) on Windows: the few commands that differ are written once below.
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

  const windows = navigator.userAgent.includes("Windows");
  /** Prints `label-value` by a command whose echo cannot match: only its execution can. */
  const say = (label, value) => type(windows
    ? `set SFV=${value}\r` + `echo ${label}-%SFV%\r`
    : `printf '${label}-%s\\n' ${value}\r`);
  const list = (directory) => type(windows ? `dir /b ${directory}\r` : `ls ${directory}\r`);
  /** Prints the size the program sees, and what the screen then shows for the given rows and columns. */
  const showSize = () => type(windows
    ? "powershell -NoProfile -Command \"$s = $Host.UI.RawUI.WindowSize; 'size-{0}-{1}' -f $s.Height, $s.Width\"\r"
    : "stty size\r");
  const sizeText = (rows, cols) => (windows ? `size-${rows}-${cols}` : `${rows} ${cols}`);
  /** Runs a command that lasts 30 seconds unless it is interrupted. */
  const longCommand = () => type(windows ? "ping -n 31 127.0.0.1 > nul\r" : "sleep 30\r");

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
    // The format (or the variable) splits the result from the command that is echoed back, so only the execution can match.
    say("sf", 42);
    await waitForScreen("sf-42");
    list("scenarios");
    await waitForScreen("pwm-setpoint.json");
  });

  it("widens the side column for the terminal, and keeps the session while the bench tab is shown", async () => {
    const workspace = document.querySelector(".workspace");
    const tab = (id) => document.getElementById(id);
    expect(workspace.classList.contains("wide")).toBe(true);
    expect(tab("terminal-toggle").getAttribute("aria-selected")).toBe("true");
    tab("bench-tab").click();
    expect(panel().hidden).toBe(true);
    expect(tab("side-bench").hidden).toBe(false);
    expect(workspace.classList.contains("wide")).toBe(false);
    say("behind", 3);  // Typed while the bench is shown: the session goes on.

    // The arrows move between the tabs: Bench, then Terminal.
    tab("bench-tab").dispatchEvent(new KeyboardEvent("keydown", {key: "ArrowRight", bubbles: true}));
    expect(sf().side.selected).toBe("terminal");
    expect(panel().hidden).toBe(false);
    await waitForScreen("behind-3");
    expect(terminal().state.running).toBe(true);
  });

  it("stacks the side column under the workspace in a narrow window, whatever the tab", async () => {
    // A page of the given width with the stylesheet of the window: the media queries follow the width of the frame.
    const columns = async (width, wide) => {
      const frame = document.createElement("iframe");
      frame.style.cssText = `position: absolute; left: -10000px; width: ${width}px; height: 600px; border: 0`;
      frame.srcdoc = `<link rel="stylesheet" href="${new URL("style.css", location.href)}">`
        + `<style>.workspace { transition: none !important; }</style>`
        + `<main class="workspace${wide ? " wide" : ""}"><section class="board"></section><aside class="side"></aside></main>`;
      const loaded = new Promise((resolve) => frame.addEventListener("load", resolve, {once: true}));
      document.body.append(frame);
      await loaded;
      const workspace = frame.contentDocument.querySelector(".workspace");
      const columns = frame.contentWindow.getComputedStyle(workspace).gridTemplateColumns.trim().split(/\s+/).map(parseFloat);
      frame.remove();
      return columns;
    };
    expect((await columns(800, false)).length).toBe(1);
    expect((await columns(800, true)).length).toBe(1);   // The terminal or the sources do not bring the second column back.
    const normal = await columns(1400, false);
    const wide = await columns(1400, true);
    expect(normal.length).toBe(2);
    expect(wide.length).toBe(2);
    expect(wide[1]).toBeGreaterThan(normal[1]);
  });

  it("does not hand the client the session of the IDE or the agent the application was started from", async () => {
    // The test is started with those variables set. Reaching the IDE of another session made the embedded client take
    // it from the first one; the settings of the user stay.
    if (windows) {
      // cmd leaves the name of an unset variable as it is.
      type("echo host-%CLAUDE_CODE_SSE_PORT%-%TERM_PROGRAM%-%CLAUDE_CODE_USE_BEDROCK%\r");
      await waitForScreen("host-%CLAUDE_CODE_SSE_PORT%-%TERM_PROGRAM%-1");
    } else {
      type("printf 'host-%s-%s-%s\\n' \"${CLAUDE_CODE_SSE_PORT:-none}\" \"${TERM_PROGRAM:-none}\" \"${CLAUDE_CODE_USE_BEDROCK:-none}\"\r");
      await waitForScreen("host-none-none-1");
    }
  });

  it("gives the program the size of the panel and follows its changes", async () => {
    const size = () => sizeText(terminal().terminal.rows, terminal().terminal.cols);
    showSize();
    await waitForScreen(size(), 30000);
    // The terminal fills the side column: a narrower column is a smaller terminal.
    document.querySelector(".workspace").style.setProperty("--side-wide", "35%");
    const before = size();
    const end = Date.now() + 10000;
    while (size() === before && Date.now() < end) await sleep(50);
    expect(size()).not.toBe(before);
    await sleep(300);  // The new size reaches the pseudoterminal through the bridge.
    showSize();
    await waitForScreen(size(), 30000);
    expect(screen().split("\n").filter((line) => line === size()).length).toBeGreaterThanOrEqual(1);
  });

  it("interrupts the program in the foreground with Ctrl+C", async () => {
    longCommand();
    await sleep(500);
    type("\x03");
    say("after", 2);
    await waitForScreen("after-2", 10000);  // Far sooner than the 30 seconds of the sleep.
  });

  it("keeps the session when another client is shown or the page attaches again", async () => {
    say("kept", 1);
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
    say("quick", 1);
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
    say("again", 1);
    await waitForScreen("again-1");
    type("exit\r");
    const end = Date.now() + 10000;
    while (terminal().state.running && Date.now() < end) await sleep(50);
    expect(terminal().state).toEqual({client: "shell", running: false, exitCode: 0});
    expect(status().textContent).toBe("exited (0)");
  });
});
