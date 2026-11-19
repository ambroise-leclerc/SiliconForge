// Agent terminal (ADR 0009, #55): the client the user installed (Claude Code, Codex, OpenCode, Mistral Vibe) or the
// shell runs in a native pseudoterminal in the project directory, drawn by xterm.js. The application names the programs
// (sf.terminal.clients) and keeps each session across page reloads and while another one is shown; the page only
// attaches to one, resizes it, types in it and hangs it up. Closing a client leaves the bench as it was.
"use strict";

window.SiliconForgeTerminal = (root, host) => {
  const $ = (selector) => root.querySelector(selector);
  const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  let list = {enabled: false, clients: []};
  let shown = null;       // {id, terminal, page}: what is attached now.
  let status = {client: null, running: false, exitCode: null};
  let adapter = null;

  const api = {
    ready: null,
    get clients() { return list.clients; },
    get enabled() { return list.enabled; },
    get state() { return {...status}; },
    /** The xterm.js Terminal now attached, or null. */
    get terminal() { return shown?.terminal ?? null; },
    open,
    hangUp,
    show,
    refresh,
  };

  const parse = (json) => JSON.parse(json);

  function theme() {
    return {background: css("--bg") || "#0d1117", foreground: css("--text") || "#e6edf3", cursor: css("--text") || "#e6edf3"};
  }

  function render() {
    const select = $("[data-select=client]");
    const open = $("[data-action=open]");
    const close = $("[data-action=hangup]");
    const readout = $("[data-readout=status]");
    const attached = status.client !== null;
    select.disabled = !list.enabled;
    open.disabled = !list.enabled || select.selectedOptions[0]?.dataset.available !== "true";
    open.textContent = attached && !status.running && select.value === status.client ? "Restart" : "Open";
    close.disabled = !attached || !status.running;
    $("[data-readout=disabled]").hidden = list.enabled;
    readout.dataset.state = !attached ? "idle" : status.running ? "running" : "exited";
    readout.textContent = !attached ? "" : status.running ? "running" : `exited (${status.exitCode})`;
  }

  function fail(error) {
    const message = $("[data-readout=error]");
    message.textContent = error ? `${error.code ?? ""} ${error.message ?? error}`.trim() : "";
    message.hidden = !error;
  }

  async function refresh() {
    await host.ready;
    list = parse(await host.call("sf.terminal.clients"));
    const select = $("[data-select=client]");
    const previous = select.value;
    select.replaceChildren(...list.clients.map((client) => {
      const option = new Option(client.available ? client.title : `${client.title} (not installed)`, client.id);
      option.dataset.available = String(client.available);
      return option;
    }));
    const first = list.clients.find((client) => client.available);
    select.value = list.clients.some((client) => client.id === previous) ? previous : (first?.id ?? "");
    render();
  }

  function detach() {
    if (!shown) return;
    shown.page?.dispose();
    shown.terminal.dispose();
    shown = null;
    adapter = null;
  }

  /** Attaches the session of a client, started on first use. `restart` starts a new one once the old one has exited. */
  async function open(id, restart = false) {
    fail(null);
    const client = list.clients.find((candidate) => candidate.id === id);
    if (!client || !client.available) throw new Error(`No terminal client ${id}`);
    show(true);
    detach();
    status = {client: id, running: true, exitCode: null};
    const terminal = new window.Terminal({cursorBlink: true, fontFamily: css("--mono"), fontSize: 13, theme: theme(), scrollback: 5000});
    shown = {id, terminal, page: null};
    render();
    try {
      const {mount} = await import("./vendor/webfront-pty/pty.mjs");
      const page = await mount($("[data-view=screen]"), {
        terminal, fit: new window.FitAddon.FitAddon(), name: `sf.terminal.${id}`, restart,
        onExit: (code) => { status = {client: id, running: false, exitCode: code}; render(); },
        onError: (error) => fail(error),
        onTruncated: () => fail({message: "Only the latest output is shown."}),
      });
      if (shown?.terminal !== terminal) { page.dispose(); return null; }
      shown.page = page;
      terminal.focus();
      return api;
    } catch (error) {
      status = {client: id, running: false, exitCode: null};
      fail(error);
      render();
      throw error;
    }
  }

  async function hangUp() {
    if (!shown?.page) return;
    await shown.page.hangUp();
  }

  function show(visible = root.hidden) {
    root.hidden = !visible;
    document.getElementById("terminal-toggle")?.setAttribute("aria-pressed", String(visible));
    if (visible) shown?.terminal.focus();
  }

  $("[data-action=open]").addEventListener("click", () => {
    const id = $("[data-select=client]").value;
    open(id, status.client === id && !status.running).catch(() => {});
  });
  $("[data-action=hangup]").addEventListener("click", () => hangUp().catch(fail));
  $("[data-select=client]").addEventListener("change", render);
  $("[data-action=close]").addEventListener("click", () => show(false));
  new MutationObserver(() => { if (shown) shown.terminal.options.theme = theme(); })
    .observe(document.documentElement, {attributes: true, attributeFilter: ["data-theme"]});

  api.ready = refresh().catch(fail);
  render();
  return api;
};
