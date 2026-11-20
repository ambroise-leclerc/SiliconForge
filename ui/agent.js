// The agent in the window (ADR 0011, #76): the client opened in the Terminal tab acts on the emulation through the MCP
// tools of the application, and each call shows here, newest first. The person can take the controls back: "Let the
// agent drive" off, the tools that change the emulation are refused and the agent is told why; reading still works.
"use strict";

window.SiliconForgeAgent = (root, host) => {
  const $ = (selector) => root.querySelector(selector);
  const maxEntries = 200;
  const entries = [];  // {at, tool, arguments, ok, summary, run?}, oldest first
  let state = {enabled: false, drives: true};
  let unseen = 0;

  const api = {
    /** The calls received since the page opened, oldest first. */
    get entries() { return [...entries]; },
    get enabled() { return state.enabled; },
    get drives() { return state.drives; },
    /** Gives the agent the controls of the emulation, or takes them back. */
    async setDrives(drives) {
      await host.call("sf.agent.drive", Boolean(drives));
      state.drives = Boolean(drives);
      render();
    },
    /** One call of a tool, pushed by the application. */
    onActivity(entry) {
      entries.push({at: new Date(), ...entry});
      if (entries.length > maxEntries) entries.splice(0, entries.length - maxEntries);
      if (!host.visible()) unseen += 1;
      render();
      host.onActivity?.(entry);
    },
    /** The journal was looked at: the count of unseen calls goes. */
    seen() {
      unseen = 0;
      render();
    },
    get unseen() { return unseen; },
    ready: null,
  };

  const time = (date) => date.toLocaleTimeString([], {hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23"});
  const argumentsText = (args) => {
    const text = JSON.stringify(args ?? {});
    return text === "{}" ? "" : text.length > 120 ? `${text.slice(0, 119)}…` : text;
  };

  function render() {
    root.hidden = !state.enabled;
    $("[data-input=drives]").checked = state.drives;
    root.dataset.drives = String(state.drives);
    $("[data-readout=empty]").hidden = entries.length > 0;
    const list = $("[data-list=activity]");
    list.replaceChildren(...entries.slice().reverse().map((entry) => {
      const item = document.createElement("li");
      item.dataset.ok = String(entry.ok);
      item.dataset.tool = entry.tool;
      const head = document.createElement("div");
      head.className = "agent-call";
      const at = document.createElement("time");
      at.textContent = time(entry.at);
      const tool = document.createElement("code");
      tool.textContent = entry.tool;
      const args = document.createElement("span");
      args.className = "agent-arguments";
      args.textContent = argumentsText(entry.arguments);
      head.append(at, tool, args);
      const result = document.createElement("div");
      result.className = "agent-result";
      result.textContent = entry.summary;
      item.append(head, result);
      return item;
    }));
    host.badge?.(unseen);
  }

  $("[data-input=drives]").addEventListener("change", (event) => {
    api.setDrives(event.target.checked).catch(() => { event.target.checked = state.drives; });
  });

  api.ready = host.ready.then(async () => {
    state = JSON.parse(await host.call("sf.agent.state"));
    render();
  }).catch(() => {});
  render();
  return api;
};
