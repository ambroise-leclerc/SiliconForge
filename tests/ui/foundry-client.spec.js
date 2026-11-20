// The exercise of docs/FOUNDRY.md (#61) with a real agent client in the terminal of the window: the client gets the
// setpoint and works alone; each question it asks (trust of the folder, permission to edit or run) is relayed to an
// operator through .siliconforge/relay/ in the project, and the keys of the answer are typed in the terminal. Once the
// bench passes, the person part of the loop is played: the diff in the editor, the run in the Bench panel, a rerun from
// the window that must give the same report.
//
// Opt-in (SILICONFORGE_FOUNDRY_CLIENT=ON): it uses the account of the user and needs an operator.
//   prompt.txt  written by the test: `<number>` on the first line, then the bottom of the screen
//   answer.txt  written by the operator: `<number>` on the first line, then the keys to type (\r, \x1b written as such)
//   result.txt  written by the test at the end: the delay, the questions and the answers
//   screen.txt  the screen of the terminal and the state of the client, every few seconds, for the operator
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 3600000;

describe("The exercise with a real client", () => {
  const sf = () => window.siliconForge;
  const terminal = () => sf().terminal;
  const bench = () => sf().bench;
  const call = (name, ...args) => webFront.cppFunction(name)(...args);
  const read = async (path) => JSON.parse(await call("sf.files.read", path));
  const write = async (path, text) => call("sf.files.write", path, text, (await read(path)).version);
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const lines = () => {
    const buffer = terminal().terminal.buffer.active;
    return Array.from({length: buffer.length}, (_, row) => buffer.getLine(row).translateToString(true));
  };
  const bottom = () => lines().slice(-terminal().terminal.rows).join("\n").replace(/\n+$/, "");
  const type = (text) => terminal().terminal.input(text, true);
  const client = "claude";
  const setpoint = "Bouton D2 relâché, la PWM de D6 doit être à 0 % ; bouton enfoncé, rien ne change. "
    + "Le scénario scenarios/pwm-setpoint.json décrit déjà cette consigne. Modifie le firmware firmware/src/pwm_setpoint.c, "
    + "compile avec `make -C firmware prebuilt` et vérifie avec `siliconforge bench run scenarios/pwm-setpoint.json` "
    + "jusqu’à un verdict pass. Ne modifie pas le scénario.";
  // A question of the client: a menu (Enter to confirm, Esc to cancel), a numbered choice, or a yes/no.
  const asking = (text) => /Enter to confirm|Esc to cancel|❯\s*\d+\.|Do you want|trust this folder|\(y\/n\)/i.test(text);
  const journal = [];
  let started = 0;

  /** Relays the question on the screen and types the answer of the operator. */
  async function relay(number, screen) {
    await write(".siliconforge/relay/prompt.txt", `${number}\n${screen}\n`);
    for (;;) {
      const answer = (await read(".siliconforge/relay/answer.txt")).content;
      const [head, ...rest] = answer.split("\n");
      if (head.trim() === String(number)) {
        const keys = rest.join("\n").replace(/\n$/, "").replaceAll("\\r", "\r").replaceAll("\\x1b", "\x1b");
        journal.push({number, keys: JSON.stringify(keys), screen});
        type(keys);
        return;
      }
      await sleep(500);
    }
  }

  async function passed() {
    const {runs} = await bench().list();
    return runs.some((run) => run.name === "pwm-setpoint" && run.verdict === "pass");
  }

  beforeAll(async () => {
    await webFront.ready;
    await terminal().ready;
    await bench().ready;
    document.getElementById("terminal-toggle").click();
    document.getElementById("editor-toggle").click();
  });

  it("the client meets the setpoint, the operator answering its questions", async () => {
    expect(terminal().clients.find((entry) => entry.id === client)?.available).toBe(true);
    started = performance.now();
    await terminal().open(client);
    let questions = 0;
    let given = false;
    let quiet = 0;
    let last = "";
    let tick = 0;
    while (!(await passed())) {
      await sleep(1000);
      const screen = bottom();
      if (++tick % 3 === 0)
        await write(".siliconforge/relay/screen.txt", `${JSON.stringify(terminal().state)}\n${screen}\n`).catch(() => {});
      if (!terminal().state.running) throw new Error(`The client ended: ${JSON.stringify(terminal().state)}\n${screen}`);
      quiet = screen === last ? quiet + 1 : 0;
      last = screen;
      if (quiet < 2) continue;  // The screen must have settled.
      if (asking(screen)) {
        await relay(++questions, screen);
        quiet = 0;
      } else if (!given && quiet >= 3) {
        type(setpoint);
        await sleep(500);
        type("\r");
        given = true;
        journal.push({number: 0, keys: "the setpoint", screen});
      }
    }
    expect(given).toBe(true);
  });

  it("the person finds the change, the failed and the passing runs, and reproduces the report from the window", async () => {
    await sf().editor.refresh();
    expect(sf().editor.files.find((file) => file.path === "firmware/src/pwm_setpoint.c")?.status).toBe("M");
    const opened = await bench().open("pwm-setpoint");
    expect(opened.report.verdict).toBe("pass");
    const result = await bench().run("pwm-setpoint.json");
    expect(result.report.verdict).toBe("pass");
    expect(result.identical).toBe(true);
    const delay = (performance.now() - started) / 1000;
    const answers = journal.filter((entry) => entry.number > 0);
    await write(".siliconforge/relay/result.txt", `${JSON.stringify({client, delaySeconds: Math.round(delay),
      interventions: 1 + answers.length, questions: answers.length, journal}, null, 2)}\n`);
    await terminal().hangUp();
  });
});
