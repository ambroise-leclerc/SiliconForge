// Qualification of the agent clients in the terminal (#55, ADR 0009): each client installed on this machine is opened
// in the project directory, must draw its interface, take typed input, follow a resize, answer Ctrl+C and be closed
// without touching the bench. A client that is not installed is skipped. The observations of each client are logged as
// "qualification <client> {...}" for docs/TERMINAL.md; they depend on the client's version and sign-in, so only what the
// terminal itself guarantees is asserted: the client draws, the close ends it, the bench works afterwards.
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 90000;

describe("The agent clients in the terminal", () => {
  const sf = () => window.siliconForge;
  const terminal = () => sf().terminal;
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const screen = () => {
    const buffer = terminal().terminal.buffer.active;
    return Array.from({length: buffer.length}, (_, row) => buffer.getLine(row).translateToString(true)).join("\n");
  };
  const until = async (condition, ms) => {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      if (condition()) return true;
      await sleep(100);
    }
    return false;
  };

  beforeAll(async () => {
    await webFront.ready;
    await terminal().ready;
  });

  for (const id of ["claude", "codex", "opencode", "vibe"]) {
    it(`runs ${id} when it is installed`, async () => {
      const client = terminal().clients.find((candidate) => candidate.id === id);
      if (!client?.available) {
        pending(`${id} is not installed`);
        return;
      }
      const seen = {};
      document.getElementById("terminal").style.height = "340px";  // The size the previous client was resized from.
      await terminal().open(id);
      seen.drawn = await until(() => screen().trim().length > 0, 30000);
      expect(seen.drawn).toBe(true);
      await sleep(2000);  // The interface of a client may take a moment to be ready for input.
      terminal().terminal.input("sfqual", true);
      seen.input = await until(() => screen().includes("sfqual"), 5000);
      const rows = terminal().terminal.rows;
      document.getElementById("terminal").style.height = "260px";
      seen.resized = await until(() => terminal().terminal.rows !== rows, 10000);
      await sleep(1000);
      seen.aliveAfterResize = terminal().state.running;
      terminal().terminal.input("\x03", true);
      await sleep(700);
      seen.stoppedByFirstCtrlC = !terminal().state.running;
      if (!seen.stoppedByFirstCtrlC) {
        terminal().terminal.input("\x03", true);
        seen.stoppedBySecondCtrlC = await until(() => !terminal().state.running, 5000);
      }
      if (terminal().state.running) await terminal().hangUp();
      expect(await until(() => !terminal().state.running, 10000)).toBe(true);
      seen.bench = (await sf().bench.list()).scenarios.length > 0;
      expect(seen.bench).toBe(true);
      console.log(`qualification ${id} ${JSON.stringify(seen)}`);
    });
  }
});
