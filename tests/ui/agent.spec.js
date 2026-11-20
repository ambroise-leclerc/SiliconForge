// The agent acts on the window through MCP (ADR 0011, #76). The shell of the terminal stands for the client: it is
// started with SILICONFORGE_MCP_URL and SILICONFORGE_MCP_TOKEN, as every client is, and calls the tools with curl, as a
// client does over the Streamable HTTP transport. What the person sees is checked: the board, the journal of the calls
// and its count on the Bench tab, the run in the Bench panel, and the switch that keeps the controls.
// The test runs in a project directory made by tests/CMakeLists.txt (scenarios/pwm-setpoint.json, the circuit and
// firmware/prebuilt/pwm_setpoint.hex). The cases share the session: run them in the order written.
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 60000;

describe("The agent in the window", () => {
  const sf = () => window.siliconForge;
  const agent = () => sf().agent;
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const screen = () => {
    const buffer = sf().terminal.terminal.buffer.active;
    return Array.from({length: buffer.length}, (_, row) => buffer.getLine(row).translateToString(true)).join("\n");
  };
  const until = async (predicate, what, ms = 20000) => {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      if (await predicate()) return;
      await sleep(50);
    }
    throw new Error(`timeout waiting for ${what}\n${screen()}`);
  };
  const type = (text) => sf().terminal.terminal.input(text, true);
  /** A call of a tool from the shell, as a client makes it. Resolves with the entry the journal shows for it. */
  const tool = async (name, args = {}) => {
    const before = agent().entries.length;
    const body = JSON.stringify({jsonrpc: "2.0", id: 1, method: "tools/call", params: {name, arguments: args}});
    type(`curl -s -X POST "$SILICONFORGE_MCP_URL" -H "Authorization: Bearer $SILICONFORGE_MCP_TOKEN" `
      + `-H 'Content-Type: application/json' -d '${body}' > /dev/null\r`);
    await until(() => agent().entries.length > before, `the call of ${name} in the journal`);
    return agent().entries.at(-1);
  };
  const tab = (id) => document.getElementById(id);
  /** A picture of the window for review, when the harness can take one: build/tests/webfront-jasmine/UI-Agent/captures. */
  const capture = async (name) => {
    if (!window.webFrontCapture) return;
    await sleep(300);
    await webFrontCapture.screenshot(name);
  };

  beforeAll(async () => {
    await webFront.ready;
    await agent().ready;
    await sf().terminal.ready;
    tab("terminal-toggle").click();
    await sf().terminal.open("shell");
  });

  it("gives the session the server of the window, and shows the journal on the bench", async () => {
    expect(agent().enabled).toBe(true);
    expect(tab("agent").hidden).toBe(false);
    type("printf 'server-%s\\n' \"${SILICONFORGE_MCP_URL#http://}\"\r");
    await until(() => /server-(127\.0\.0\.1|\[::1\]):\d+\/mcp\//.test(screen()), "the URL of the server in the session");
  });

  it("loads the firmware the agent names, and the window runs it", async () => {
    const entry = await tool("load_firmware", {path: "firmware/prebuilt/pwm_setpoint.hex"});
    expect(entry.ok).toBe(true);
    expect(entry.tool).toBe("load_firmware");
    await sf().waitFor(() => sf().state?.firmware === "pwm_setpoint.hex" && sf().state.running, 10000);
    // The person watches the terminal: the calls are counted on the Bench tab.
    expect(tab("bench-tab").dataset.unseen).toBe("1");
  });

  it("presses the button of the breadboard: the LED of D13 lights, and the measure follows the setpoint", async () => {
    const pressed = await tool("set_component", {component: "button-d2", state: "pressed", value: 1});
    expect(pressed.ok).toBe(true);
    await sf().waitForPin("D13", 1, 5000);
    expect(sf().component("button-d2").pressed).toBeTruthy();
    const measured = await tool("measure", {pin: "D6", quantity: "dutyCycle", window: 0.002});
    expect(measured.ok).toBe(true);
    expect(JSON.parse(measured.summary).value).toBeCloseTo(75, 1);
  });

  it("checks every argument before it changes the emulation", async () => {
    const cycles = sf().state.cycles;
    const refused = await tool("load_firmware", {path: "firmware/prebuilt/pwm_setpoint.hex", run: "false"});
    expect(refused.ok).toBe(false);
    expect(refused.summary).toContain("run");
    expect(sf().state.running).toBe(true);  // Loading would have paused it and gone back to cycle 0.
    expect(sf().state.cycles).toBeGreaterThanOrEqual(cycles);
  });

  it("refuses a measure whose capture restarted while it waited", async () => {
    const before = agent().entries.length;
    const body = (name, args) => JSON.stringify({jsonrpc: "2.0", id: 1, method: "tools/call", params: {name, arguments: args}});
    const post = (name, args) => `curl -s -X POST "$SILICONFORGE_MCP_URL" -H "Authorization: Bearer $SILICONFORGE_MCP_TOKEN" `
      + `-H 'Content-Type: application/json' -d '${body(name, args)}' > /dev/null`;
    // A measure over two simulated seconds, and a reset half a second after it started.
    type(`(${post("measure", {pin: "D6", quantity: "dutyCycle", window: 2})} &) ; sleep 0.5 ; ${post("reset", {})}\r`);
    await until(() => agent().entries.length >= before + 2 && agent().entries.slice(before).some((entry) => entry.tool === "measure"), "the measure and the reset");
    const measure = agent().entries.slice(before).find((entry) => entry.tool === "measure");
    expect(measure.ok).toBe(false);
    expect(measure.summary).toContain("capture restarted");
  });

  it("refuses the changes while the person keeps the controls, and still answers the reads", async () => {
    tab("bench-tab").click();
    expect(tab("bench-tab").dataset.unseen).toBeUndefined();
    const drives = tab("agent").querySelector("[data-input=drives]");
    drives.click();
    await until(() => agent().drives === false, "the switch");
    const cycles = sf().state.cycles;
    const refused = await tool("reset");
    expect(refused.ok).toBe(false);
    expect(refused.summary).toContain("keeps the controls");
    expect(sf().state.cycles).toBeGreaterThanOrEqual(cycles);  // No reset happened.
    expect((await tool("read_pins")).ok).toBe(true);
    const rows = [...tab("agent").querySelectorAll("[data-list=activity] li")];
    expect(rows[0].dataset.tool).toBe("read_pins");  // Newest first.
    expect(rows[1].dataset.ok).toBe("false");
    await capture("agent-journal");
    drives.click();
    await until(() => agent().drives === true, "the switch back");
  });

  it("opens the run of a scenario the agent starts in the Bench panel", async () => {
    const entry = await tool("bench_run", {scenario: "pwm-setpoint.json"});
    expect(entry.ok).toBe(true);
    expect(entry.run).toBe("pwm-setpoint");
    await until(() => sf().bench.result?.run === "pwm-setpoint", "the run in the Bench panel");
    expect(document.querySelector("#bench [data-readout=verdict]").textContent).toBe("pass");
  });

  it("refuses a call without the token of the window", async () => {
    tab("terminal-toggle").click();
    type("curl -s -o /dev/null -w 'status-%{http_code}\\n' -X POST \"$SILICONFORGE_MCP_URL\" -H 'Authorization: Bearer wrong' -d '{}'\r");
    await until(() => screen().includes("status-401"), "a refusal");
  });

  it("hands a run of the command line to the window, which shows it and runs what it tested (#80)", async () => {
    tab("terminal-toggle").click();
    const cycles = sf().state.cycles;
    type("siliconforge bench run scenarios/pwm-setpoint.json; printf 'bench-exit-%s\\n' $?\r");
    await until(() => screen().includes("bench-exit-0"), "the end of the command");
    expect(screen()).toContain("PWM setpoint on D6: pass");
    expect(screen()).toContain("Window: firmware/prebuilt/pwm_setpoint.hex loaded and running");
    const entry = agent().entries.at(-1);
    expect(entry.tool).toBe("bench_run");
    expect(entry.arguments).toEqual({scenario: "pwm-setpoint.json", load: true});
    await until(() => sf().bench.result?.run === "pwm-setpoint", "the run in the Bench panel");
    // Loaded again from reset, and running.
    await sf().waitFor(() => sf().state.running && sf().state.cycles < cycles && sf().state.firmware === "pwm_setpoint.hex", 10000);
  });

  afterAll(async () => {
    await sf().terminal.hangUp();
  });
});
