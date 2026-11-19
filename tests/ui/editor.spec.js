// The editor of the project sources in the native window (#56): a file of the project directory opened, edited and saved;
// a change made behind its back reloading a clean buffer; both versions shown, and the local text kept, when the buffer
// holds edits; the Git changes the agent leaves. The shell of the agent terminal stands for the agent: it writes files in
// the project directory the way a client does.
// The test runs in the project directory made by tests/CMakeLists.txt (main.cpp, src/pwm.cpp, notes.txt, in a Git
// repository of their own when `git` is installed). The cases share that directory: run them in the order written.
jasmine.getEnv().configure({random: false});
jasmine.DEFAULT_TIMEOUT_INTERVAL = 30000;

describe("The editor of the project sources in the window", () => {
  const sf = () => window.siliconForge;
  const windows = navigator.userAgent.includes("Windows");
  const editor = () => sf().editor;
  const panel = () => document.getElementById("editor");
  const part = (selector) => panel().querySelector(selector);
  const call = (name, ...args) => webFront.cppFunction(name)(...args);
  const read = async (path) => JSON.parse(await call("sf.files.read", path));
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const until = async (predicate, what, ms = 10000) => {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      if (await predicate()) return;
      await sleep(50);
    }
    throw new Error(`timeout waiting for ${what}`);
  };
  /** Changes a file the way another program does: not through the editor, over the version it has now. */
  const writeElsewhere = async (path, text) => {
    const {version} = await read(path);
    await call("sf.files.write", path, text, version);
  };
  const rejection = async (promise) => {
    try { await promise; } catch (error) { return error; }
    throw new Error("the call did not reject");
  };
  /** A picture of the window for review, when the harness can take one: build/tests/webfront-jasmine/UI-Editor/captures. */
  const capture = async (name) => {
    if (!window.webFrontCapture) return;
    panel().scrollIntoView({block: "end"});
    await sleep(200);
    await webFrontCapture.screenshot(name);
  };
  const statusOf = (path) => editor().files.find((file) => file.path === path)?.status;

  beforeAll(async () => {
    await webFront.ready;
    document.getElementById("editor-toggle").click();
    await editor().refresh();
    editor().setFilter({changedOnly: false, text: ""});
  });

  it("lists the files of the project directory, not the Git folder", () => {
    expect(panel().hidden).toBe(false);
    const paths = editor().files.map((file) => file.path);
    expect(paths).toContain("main.cpp");
    expect(paths).toContain("src/pwm.cpp");
    expect(paths).toContain("notes.txt");
    expect(paths.some((path) => path.startsWith(".git"))).toBe(false);
    const rows = [...panel().querySelectorAll("[data-view=files] button.file")].map((button) => button.dataset.path);
    expect(rows).toEqual(jasmine.arrayContaining(["main.cpp", "src/pwm.cpp", "notes.txt"]));
    const box = panel().getBoundingClientRect();
    expect(box.width).toBeGreaterThan(300);
    expect(box.height).toBeGreaterThan(150);
  });

  it("opens, edits and saves a file", async () => {
    await editor().open("main.cpp");
    expect(editor().state.path).toBe("main.cpp");
    expect(editor().text).toBe("int main() {\n  return 0;\n}\n");
    expect(part("[data-view=text]").value).toBe("int main() {\n  return 0;\n}\n");
    expect(part("[data-action=save]").disabled).toBe(true);

    const area = part("[data-view=text]");
    area.value = "int main() {\n  return 1;\n}\n";
    area.dispatchEvent(new Event("input", {bubbles: true}));  // What typing does.
    expect(editor().state.dirty).toBe(true);
    expect(part("[data-readout=state]").textContent).toBe("unsaved");
    expect(part("[data-action=save]").disabled).toBe(false);

    expect(await editor().save()).toBe(true);
    expect(editor().state.dirty).toBe(false);
    expect(part("[data-readout=state]").textContent).toBe("saved");
    expect((await read("main.cpp")).content).toBe("int main() {\n  return 1;\n}\n");
  });

  it("colors the text by the language of the file, under the text being edited", async () => {
    await editor().open("main.cpp");
    expect(editor().language).toBe("cpp");
    const area = part("[data-view=text]");
    const code = part("[data-view=highlight]");
    const tokens = (type) => [...code.querySelectorAll(`.token.${type}`)].map((token) => token.textContent);
    expect(code.textContent).toBe(editor().text + "\n");  // The same text, so the same boxes.
    expect(tokens("keyword")).toEqual(jasmine.arrayContaining(["int", "return"]));
    expect(tokens("number")).toContain("1");
    const keyword = [...code.querySelectorAll(".token.keyword")][0];
    expect(getComputedStyle(keyword).color).not.toBe(getComputedStyle(code).color);
    // The textarea only carries the caret and the selection: its own text is not drawn, the copy under it is.
    expect(getComputedStyle(area).webkitTextFillColor).toBe("rgba(0, 0, 0, 0)");
    for (const property of ["fontFamily", "fontSize", "lineHeight", "tabSize", "paddingTop", "paddingLeft"]) {
      expect(getComputedStyle(code.parentElement)[property]).withContext(property).toBe(getComputedStyle(area)[property]);
    }
    expect(code.parentElement.getBoundingClientRect().width).toBe(area.getBoundingClientRect().width);

    // Typing recolors, and scrolling the text scrolls the colored copy with it.
    const original = area.value;
    area.value = "// a comment\n" + "int x = 0;\n".repeat(200);
    area.dispatchEvent(new Event("input", {bubbles: true}));
    await until(() => tokens("comment").includes("// a comment"), "the new comment colored");
    area.scrollTop = 300;
    area.dispatchEvent(new Event("scroll"));
    expect(code.parentElement.scrollTop).toBe(area.scrollTop);
    expect(area.scrollTop).toBeGreaterThan(200);
    area.value = original;
    area.dispatchEvent(new Event("input", {bubbles: true}));
    await until(() => tokens("comment").length === 0, "the colors back to the saved text");
    expect(editor().state.dirty).toBe(false);
    document.documentElement.dataset.theme = "dark";
    await capture("editor-colors-dark");
    document.documentElement.dataset.theme = "light";
    await capture("editor-colors-light");
    delete document.documentElement.dataset.theme;

    // A file whose language is not known stays plain, and a language change follows the file.
    await editor().open("notes.txt");
    expect(editor().language).toBe("plain");
    expect(code.textContent).toBe("Notes for the agent\n\n");
    expect(code.querySelector(".token")).toBe(null);
    await editor().open("src/pwm.cpp");
    expect(editor().language).toBe("cpp");
  });

  it("reloads a clean buffer that changed on the disk", async () => {
    await editor().open("src/pwm.cpp");
    expect(editor().text).toBe("// PWM at 25 %\n");
    await writeElsewhere("src/pwm.cpp", "// PWM at 75 %\n");
    await editor().poll();
    expect(editor().text).toBe("// PWM at 75 %\n");
    expect(editor().state.dirty).toBe(false);
    expect(editor().state.stale).toBe(false);
    expect(part("[data-view=text]").value).toBe("// PWM at 75 %\n");
  });

  it("flags a buffer with edits when the disk changed, and replaces nothing", async () => {
    editor().text = "// my edit\n";
    await writeElsewhere("src/pwm.cpp", "// written by the agent\n");
    await editor().poll();
    expect(editor().state.stale).toBe(true);
    expect(editor().text).toBe("// my edit\n");
    expect(part("[data-readout=notice]").hidden).toBe(false);
    expect((await read("src/pwm.cpp")).content).toBe("// written by the agent\n");
  });

  it("shows both versions before saving, and keeps the local text recoverable", async () => {
    expect(await editor().save()).toBe(false);  // Refused: nothing is written.
    expect(editor().state.conflict).toBe(true);
    expect(editor().conflict).toEqual({mine: "// my edit\n", disk: "// written by the agent\n"});
    expect(part("[data-view=conflict]").hidden).toBe(false);
    expect(part("[data-view=mine]").value).toBe("// my edit\n");
    expect(part("[data-view=disk]").value).toBe("// written by the agent\n");
    await capture("editor-both-versions");
    expect((await read("src/pwm.cpp")).content).toBe("// written by the agent\n");

    // Cancel decides nothing: the dialog goes, the edits stay, the file is untouched.
    await editor().cancel();
    expect(editor().state.conflict).toBe(false);
    expect(editor().text).toBe("// my edit\n");
    expect((await read("src/pwm.cpp")).content).toBe("// written by the agent\n");

    // Taking the disk version keeps the local text apart ...
    expect(await editor().save()).toBe(false);
    await editor().useDisk();
    expect(editor().text).toBe("// written by the agent\n");
    expect(editor().state.dirty).toBe(false);
    expect(editor().state.recovered).toBe("// my edit\n");
    expect(part("[data-view=recovered]").hidden).toBe(false);
    expect(localStorage.getItem("siliconforge.recovered.src/pwm.cpp")).toBe("// my edit\n");

    // ... and Restore brings it back, ready to be saved over the version now on the disk.
    expect(editor().restore()).toBe(true);
    expect(editor().text).toBe("// my edit\n");
    expect(editor().state.dirty).toBe(true);
    expect(editor().state.recovered).toBe(null);
    expect(localStorage.getItem("siliconforge.recovered.src/pwm.cpp")).toBe(null);
    expect(await editor().save()).toBe(true);
    expect((await read("src/pwm.cpp")).content).toBe("// my edit\n");
  });

  it("overwrites the disk version when the person chooses their own", async () => {
    editor().text = "// mine again\n";
    await writeElsewhere("src/pwm.cpp", "// the agent again\n");
    expect(await editor().save()).toBe(false);
    expect(editor().conflict.disk).toBe("// the agent again\n");
    await editor().keepMine();
    expect(editor().state.conflict).toBe(false);
    expect(editor().state.dirty).toBe(false);
    expect((await read("src/pwm.cpp")).content).toBe("// mine again\n");
  });

  it("refuses a file outside the project, the Git folder, and files that are not text", async () => {
    const refused = ["../outside.txt", "/etc/passwd", ".git/config", "src/../../outside.txt", "nothing.txt"];
    // Windows names the same file in other ways: a drive, a backslash, a trailing dot or space, a data stream, a short name.
    if (windows) refused.push("C:/Windows/win.ini", "src\\..\\..\\outside.txt", "\\Windows\\win.ini", ".git./config", ".git /config", "main.cpp::$DATA", "GIT~1/config");
    for (const path of refused) {
      const error = await rejection(call("sf.files.read", path));
      expect(error.code).withContext(path).toBe("siliconforge.unknownFile");
      expect((await rejection(call("sf.files.write", path, "x", "0-0"))).code).withContext(path).toBe("siliconforge.unknownFile");
    }
  });

  it("shows what the agent changed in the project", async () => {
    if (!editor().git) pending("git is not installed: the project directory is not a repository");
    editor().setFilter({changedOnly: true, text: ""});
    await editor().open("main.cpp");
    // The agent: a shell of the terminal, writing in the project directory as a client does.
    document.getElementById("terminal-toggle").click();
    await sf().terminal.ready;
    await sf().terminal.open("shell");
    // The shell is sh on Linux and macOS, cmd.exe (ConPTY) on Windows, where PowerShell writes the LF line ends the files have.
    sf().terminal.terminal.input(windows
      ? "powershell -NoProfile -Command \"[IO.File]::WriteAllText('agent.cpp', 'int agent;' + [char]10); [IO.File]::AppendAllText('main.cpp', '// by the agent' + [char]10)\"\r"
      : "printf 'int agent;\\n' > agent.cpp && printf '// by the agent\\n' >> main.cpp\r", true);

    await until(async () => { await editor().refresh(); return statusOf("agent.cpp") === "?"; }, "the new file in the list");
    expect(statusOf("main.cpp")).toBe("M");
    expect(statusOf("src/pwm.cpp")).toBe("M");
    expect(statusOf("notes.txt")).toBe("");
    const shown = [...panel().querySelectorAll("[data-view=files] button.file")].map((button) => button.dataset.path);
    expect(shown).toEqual(jasmine.arrayContaining(["main.cpp", "agent.cpp", "src/pwm.cpp"]));
    expect(shown).not.toContain("notes.txt");  // "Changed only".

    // main.cpp was open and clean: it picks the agent's line up.
    await until(async () => { await editor().poll(); return editor().text.includes("// by the agent"); }, "the agent's line in the buffer");
    expect(editor().state.dirty).toBe(false);

    await editor().view("diff");
    const diff = part("[data-view=diff]");
    expect(diff.hidden).toBe(false);
    expect(part("[data-view=text]").hidden).toBe(true);
    const rows = (kind) => [...diff.querySelectorAll(`.${kind}`)].map((row) => row.textContent.trimEnd());
    expect(rows("del")).toContain("-  return 0;");
    expect(rows("add")).toEqual(jasmine.arrayContaining(["+  return 1;", "+// by the agent"]));
    expect(rows("hunk").length).toBeGreaterThan(0);
    await capture("editor-changes");

    await editor().open("agent.cpp");
    await editor().view("diff");
    expect(rows("add")).toContain("+int agent;");
    await editor().view("edit");
    expect(editor().text).toBe("int agent;\n");
  });

  it("shows a file deleted by the agent as a change, without text to edit", async () => {
    if (!editor().git) pending("git is not installed: the project directory is not a repository");
    sf().terminal.terminal.input(windows ? "del notes.txt\r" : "rm notes.txt\r", true);
    await until(async () => { await editor().refresh(); return statusOf("notes.txt") === "D"; }, "the deletion in the list");
    await editor().open("notes.txt");
    expect(editor().state.deleted).toBe(true);
    expect(editor().state.view).toBe("diff");
    expect(part("[data-view=text]").hidden).toBe(true);
    const removed = [...part("[data-view=diff]").querySelectorAll(".del")].map((row) => row.textContent.trimEnd());
    expect(removed.some((line) => line.startsWith("-Notes"))).toBe(true);
  });

  afterAll(async () => {
    await sf().terminal.hangUp();
  });
});
