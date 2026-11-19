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
  let latest = 0;         // Ticket of the latest open().
  let queue = Promise.resolve();  // The openings, one after the other.

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

  /**
   * Takes a terminal off the screen now and disposes it a little later: xterm.js sets its renderer up asynchronously,
   * and a terminal disposed right after open() fails in a timer of its own.
   */
  function release(terminal) {
    terminal.element?.remove();
    setTimeout(() => terminal.dispose(), 250);
  }

  function detach() {
    if (!shown) return;
    shown.page?.dispose();
    release(shown.terminal);
    shown = null;
  }

  /**
   * Attaches the session of a client, started on first use. `restart` starts a new one once the old one has exited.
   * Openings run one after the other, and only the latest one counts: mount() cannot be cancelled and drives the
   * terminal and the element it was given, so an opening that a later call replaced is let finish, then discarded
   * without touching the status, the errors or the screen of the client now shown.
   */
  function open(id, restart = false) {
    const client = list.clients.find((candidate) => candidate.id === id);
    if (!client || !client.available) return Promise.reject(new Error(`No terminal client ${id}`));
    const ticket = ++latest;
    const current = () => ticket === latest;
    fail(null);
    show(true);
    status = {client: id, running: true, exitCode: null};
    render();
    const run = async () => {
      if (!current()) return null;  // Replaced before it started: nothing was created.
      detach();
      const terminal = new window.Terminal({cursorBlink: true, fontFamily: css("--mono"), fontSize: 13, theme: theme(), scrollback: 5000});
      shown = {id, terminal, page: null};
      let page = null;
      try {
        const {mount} = await import("./vendor/webfront-pty/pty.mjs");
        page = await mount($("[data-view=screen]"), {
          terminal, fit: new window.FitAddon.FitAddon(), name: `sf.terminal.${id}`, restart,
          onExit: (code) => { if (!current()) return; status = {client: id, running: false, exitCode: code}; render(); },
          onError: (error) => { if (current()) fail(error); },
          onTruncated: () => { if (current()) fail({message: "Only the latest output is shown."}); },
        });
      } catch (error) {
        shown = shown?.terminal === terminal ? null : shown;
        release(terminal);
        if (!current()) return null;
        status = {client: id, running: false, exitCode: null};
        fail(error);
        render();
        throw error;
      }
      if (!current()) {  // Replaced while it mounted: the session stays in the application, only the view goes.
        page.dispose();
        release(terminal);
        if (shown?.terminal === terminal) shown = null;
        return null;
      }
      shown.page = page;
      terminal.focus();
      return api;
    };
    const result = queue.then(run);
    queue = result.catch(() => {});
    return result;
  }

  async function hangUp() {
    await queue;  // An opening in progress has no session to end yet.
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
