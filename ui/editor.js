// Sources of the project (ADR 0009, #56): the files of the directory the agent works in, edited and saved from the
// window, with the Git changes against the last commit. The application reads and writes the files (sf.files.*); this
// page keeps one buffer per opened file and watches the disk.
//
// A file changed on disk behind the editor (by the agent, typically) reloads when its buffer is clean. When the buffer
// holds unsaved edits, nothing is replaced: the save is refused by the application, and both versions are shown side by
// side. Choosing the disk version keeps the local text, which Restore brings back, and which is also kept in the
// browser storage of the page until it is restored or discarded.
"use strict";

window.SiliconForgeEditor = (root, host) => {
  const $ = (selector) => root.querySelector(selector);
  const pollInterval = 1500;
  const storageKey = (path) => `siliconforge.recovered.${path}`;

  let listing = {name: "", git: false, truncated: false, files: []};
  let listingJson = "";
  const buffers = new Map();  // path -> {path, base, version, text, lineEndings, error, deleted, stale, recovered}
  let current = null;         // The buffer shown.
  let mode = "edit";          // "edit" or "diff".
  let conflict = null;        // {buffer, mine, disk, diskVersion}: the two versions of a refused save.
  let filter = {text: "", changedOnly: true};
  let timer = null;
  let ticks = 0;
  let chain = Promise.resolve();  // Reads and saves, one after the other.
  let notice = "";

  const parse = (json) => JSON.parse(json);
  const dirty = (buffer) => buffer && buffer.error === null && !buffer.deleted && buffer.text !== buffer.base;

  const api = {
    ready: null,
    get files() { return listing.files.map((file) => ({...file})); },
    get git() { return listing.git; },
    /** What the page shows: the file, whether it holds unsaved edits, the view, the pending conflict and the kept text. */
    get state() {
      return {
        path: current?.path ?? null, dirty: dirty(current) ?? false, stale: current?.stale ?? false, deleted: current?.deleted ?? false,
        error: current?.error ?? null, view: mode, conflict: conflict !== null, recovered: current?.recovered ?? null, notice,
      };
    },
    get text() { return current ? current.text : ""; },
    set text(value) { edit(value); },
    /** The two versions of a refused save: {mine, disk}, or null. */
    get conflict() { return conflict ? {mine: conflict.mine, disk: conflict.disk} : null; },
    open: (path) => enqueue(() => openFile(path)),
    save: () => enqueue(save),
    keepMine: () => enqueue(() => resolve("mine")),
    useDisk: () => enqueue(() => resolve("disk")),
    cancel: () => enqueue(() => resolve("cancel")),
    restore,
    discardRecovered,
    view,
    poll: () => enqueue(poll),
    refresh: () => enqueue(refreshList),
    setFilter(next) { filter = {...filter, ...next}; $("[data-input=changed]").checked = filter.changedOnly; $("[data-input=filter]").value = filter.text; renderList(); },
    show,
    close,
  };

  function enqueue(task) {
    const result = chain.then(task);
    chain = result.catch(() => {});
    return result;
  }

  function fail(error) {
    const message = $("[data-readout=error]");
    message.textContent = error ? `${error.code ?? ""} ${error.message ?? error}`.trim() : "";
    message.hidden = !error;
  }

  // Drawing ----------------------------------------------------------------------------------------------------------

  function renderList() {
    const text = filter.text.trim().toLowerCase();
    const shown = listing.files.filter((file) => (!filter.changedOnly || file.status !== "") && (!text || file.path.toLowerCase().includes(text)));
    const list = $("[data-view=files]");
    list.replaceChildren(...shown.map((file) => {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.className = "file";
      button.dataset.path = file.path;
      if (current?.path === file.path) button.setAttribute("aria-current", "true");
      const badge = document.createElement("span");
      badge.className = "badge";
      badge.dataset.status = file.status;
      badge.textContent = file.status || " ";
      badge.title = {M: "modified", A: "added", D: "deleted", R: "renamed", U: "conflict", "?": "not tracked"}[file.status] ?? "unchanged";
      const name = document.createElement("span");
      name.className = "name";
      name.textContent = file.path;
      button.append(badge, name);
      if (dirty(buffers.get(file.path))) {
        const mark = document.createElement("span");
        mark.className = "unsaved";
        mark.textContent = "●";
        mark.title = "unsaved edits";
        button.append(mark);
      }
      button.addEventListener("click", () => api.open(file.path).catch(fail));
      item.append(button);
      return item;
    }));
    const empty = $("[data-readout=empty]");
    empty.hidden = shown.length > 0;
    empty.textContent = listing.files.length === 0 ? "No file in the project directory."
      : filter.changedOnly && !text ? (listing.git ? "No change against the last commit. Untick “Changed only” to list every file." : "Not a Git repository: no changes to show. Untick “Changed only” to list every file.")
      : "No file matches.";
    $("[data-readout=project]").textContent = `${listing.name}${listing.git ? "" : " (no Git)"}${listing.truncated ? " (list cut)" : ""}`;
  }

  function renderBar() {
    const buffer = current;
    $("[data-readout=file]").textContent = buffer ? buffer.path : "No file open";
    const state = $("[data-readout=state]");
    state.textContent = !buffer ? "" : buffer.error ? "not shown" : buffer.deleted ? "deleted" : dirty(buffer) ? "unsaved" : "saved";
    state.dataset.state = !buffer ? "none" : buffer.error ? "error" : buffer.deleted ? "deleted" : dirty(buffer) ? "dirty" : "clean";
    $("[data-action=save]").disabled = !buffer || !dirty(buffer) || conflict !== null || buffer.lineEndings === "mixed";
    for (const tab of root.querySelectorAll("[data-mode]")) {
      tab.setAttribute("aria-selected", String(tab.dataset.mode === mode));
      tab.disabled = !buffer;
    }
    const messages = [];
    if (buffer?.gone) messages.push("This file is gone from the disk, or is no longer text. Your text stays in the editor.");
    if (buffer?.stale) messages.push("This file changed on disk. Saving will show both versions before anything is written.");
    if (buffer?.lineEndings === "mixed") messages.push("This file mixes line endings: it is shown, not editable, so that saving cannot alter them.");
    if (buffer?.error) messages.push(buffer.error);
    notice = messages.join(" ");
    const area = $("[data-readout=notice]");
    area.textContent = notice;
    area.hidden = notice === "";
    const kept = $("[data-view=recovered]");
    kept.hidden = !buffer?.recovered;
  }

  function renderBody() {
    const text = $("[data-view=text]");
    const diff = $("[data-view=diff]");
    const buffer = current;
    const showText = mode === "edit" && buffer && !buffer.error && !buffer.deleted && conflict === null;
    text.hidden = !(mode === "edit" && conflict === null);
    text.disabled = !showText || buffer.lineEndings === "mixed";
    if (buffer && !buffer.error && !buffer.deleted && text.value !== buffer.text) text.value = buffer.text;
    if (!buffer || buffer.error || buffer.deleted) text.value = "";
    diff.hidden = !(mode === "diff" && conflict === null);
    $("[data-view=conflict]").hidden = conflict === null;
    if (conflict) {
      $("[data-view=mine]").value = conflict.mine;
      $("[data-view=disk]").value = conflict.disk ?? "";
      $("[data-readout=gone]").hidden = conflict.disk !== null;
      $("[data-action=keep-mine]").disabled = conflict.disk === null;
      $("[data-action=use-disk]").disabled = conflict.disk === null;
    }
  }

  function render() {
    renderList();
    renderBar();
    renderBody();
  }

  function drawDiff(result) {
    const pre = $("[data-view=diff]");
    pre.replaceChildren();
    const lines = result.diff === "" ? [] : result.diff.replace(/\n$/, "").split("\n");
    if (lines.length === 0) {
      pre.textContent = !result.git ? "Not a Git repository: there is nothing to compare this file with." : "No change against the last commit.";
      pre.dataset.empty = "true";
      return;
    }
    delete pre.dataset.empty;
    for (const line of lines) {
      const row = document.createElement("span");
      row.className = line.startsWith("@@") ? "hunk" : line.startsWith("+++") || line.startsWith("---") || line.startsWith("diff ") || line.startsWith("index ")
        || line.startsWith("new file") || line.startsWith("deleted file") || line.startsWith("\\") ? "meta" : line.startsWith("+") ? "add" : line.startsWith("-") ? "del" : "ctx";
      row.textContent = line + "\n";
      pre.append(row);
    }
    if (result.truncated) {
      const row = document.createElement("span");
      row.className = "meta";
      row.textContent = "… the diff is cut here.\n";
      pre.append(row);
    }
  }

  // Files ------------------------------------------------------------------------------------------------------------

  async function refreshList() {
    await host.ready;
    const json = await host.call("sf.files.list");
    if (json === listingJson) return;
    listingJson = json;
    listing = parse(json);
    renderList();
  }

  const storedRecovery = (path) => {
    try { return localStorage.getItem(storageKey(path)); } catch { return null; }
  };
  const storeRecovery = (path, text) => {
    try {
      if (text === null) localStorage.removeItem(storageKey(path)); else localStorage.setItem(storageKey(path), text);
    } catch { /* Storage may be unavailable: the text stays in the page. */ }
  };

  /** Reads `path` from disk into a buffer. */
  async function load(path) {
    const buffer = {path, base: "", version: null, text: "", lineEndings: "lf", error: null, deleted: false, stale: false, gone: false, recovered: storedRecovery(path)};
    try {
      const file = parse(await host.call("sf.files.read", path));
      buffer.version = file.version;
      buffer.lineEndings = file.lineEndings;
      const probe = document.createElement("textarea");  // The text as a textarea holds it: its line ends are "\n".
      probe.value = file.content;
      buffer.base = buffer.text = probe.value;
    } catch (error) {
      if (listing.files.find((file) => file.path === path)?.status === "D") buffer.deleted = true;
      else buffer.error = error.code === "siliconforge.notText" ? "This file is not UTF-8 text: its changes can still be shown."
        : error.code === "siliconforge.tooLarge" ? "This file is larger than 1 MiB: its changes can still be shown." : `${error.code ?? ""} ${error.message ?? error}`.trim();
    }
    return buffer;
  }

  async function openFile(path) {
    fail(null);
    await host.ready;
    syncBuffer();
    let buffer = buffers.get(path);
    if (!buffer) {
      buffer = await load(path);
      buffers.set(path, buffer);
    }
    current = buffer;
    conflict = null;
    if (buffer.deleted || buffer.error) mode = "diff";
    render();
    if (mode === "diff") await loadDiff();
    await poll();
    return api;
  }

  /** The textarea is the buffer's text while the file is shown. */
  function syncBuffer() {
    if (current && !current.error && !current.deleted && conflict === null && mode === "edit") current.text = $("[data-view=text]").value;
  }

  function edit(value) {
    if (!current || current.error || current.deleted || conflict !== null) throw new Error("No file to edit");
    current.text = value;
    $("[data-view=text]").value = value;
    renderBar();
    renderList();
  }

  async function view(next) {
    syncBuffer();
    mode = next;
    render();
    if (next === "diff") await loadDiff();
  }

  async function loadDiff() {
    const buffer = current;
    if (!buffer) return;
    try {
      const result = parse(await host.call("sf.files.diff", buffer.path));
      if (current === buffer && mode === "diff") drawDiff(result);
    } catch (error) {
      if (current === buffer) fail(error);
    }
  }

  // Saving and the two versions ---------------------------------------------------------------------------------------

  async function save() {
    syncBuffer();
    const buffer = current;
    if (!buffer || !dirty(buffer) || conflict !== null) return false;
    fail(null);
    return write(buffer, buffer.version);
  }

  /** Writes the text of `buffer` over the file if it is still `version`; otherwise shows both versions. */
  async function write(buffer, version) {
    const mine = buffer.text;
    try {
      const result = parse(await host.call("sf.files.write", buffer.path, mine, version));
      buffer.base = mine;
      buffer.version = result.version;
      buffer.stale = false;
      conflict = null;
      render();
      await refreshList();
      if (mode === "diff") await loadDiff();
      return true;
    } catch (error) {
      if (error.code !== "siliconforge.fileChanged" && error.code !== "siliconforge.unknownFile" && error.code !== "siliconforge.notText") {
        fail(error);
        throw error;
      }
      let disk = null, diskVersion = null;
      try {
        const file = parse(await host.call("sf.files.read", buffer.path));
        const probe = document.createElement("textarea");
        probe.value = file.content;
        disk = probe.value;
        diskVersion = file.version;
      } catch { /* The file is gone or no longer text. */ }
      conflict = {buffer, mine, disk, diskVersion};
      buffer.stale = true;
      render();
      return false;
    }
  }

  async function resolve(choice) {
    if (!conflict) return false;
    const {buffer, mine, disk, diskVersion} = conflict;
    if (choice === "cancel") {
      conflict = null;
      render();
      return false;
    }
    if (choice === "mine") {
      conflict = null;
      return write(buffer, diskVersion);  // On top of the version just read; refused again if it moved once more.
    }
    // The disk version replaces the buffer; the local text is kept apart so that nothing typed is lost.
    buffer.recovered = mine;
    storeRecovery(buffer.path, mine);
    buffer.base = buffer.text = disk;
    buffer.version = diskVersion;
    buffer.stale = false;
    conflict = null;
    render();
    await refreshList();
    return true;
  }

  /** Puts the local text kept by "use the disk version" back in the buffer, to be saved over the current disk version. */
  function restore() {
    if (!current?.recovered) return false;
    current.text = current.recovered;
    current.recovered = null;
    storeRecovery(current.path, null);
    render();
    return true;
  }

  function discardRecovered() {
    if (!current?.recovered) return;
    current.recovered = null;
    storeRecovery(current.path, null);
    render();
  }

  // Watching the disk --------------------------------------------------------------------------------------------------

  /** Compares the shown file with the disk: a clean buffer reloads, an edited one is flagged. */
  async function poll() {
    ticks += 1;
    const buffer = current;
    if (ticks % 2 === 0) await refreshList().catch(() => {});
    if (buffer && !buffer.error && conflict === null) {
      let answer;
      try { answer = parse(await host.call("sf.files.version", buffer.path)); } catch { answer = null; }
      if (current === buffer && conflict === null && answer) {
        if (answer.version === null) {
          buffer.gone = !buffer.deleted && !buffer.error;  // Removed, or no longer text: the buffer keeps what it holds.
          render();
        } else if (answer.version !== buffer.version || buffer.gone) {
          buffer.gone = false;
          syncBuffer();
          if (!dirty(buffer)) {
            const fresh = await load(buffer.path);
            if (current === buffer && !dirty(buffer) && buffer.version !== fresh.version) {
              Object.assign(buffer, {base: fresh.base, text: fresh.text, version: fresh.version, lineEndings: fresh.lineEndings, error: fresh.error, deleted: fresh.deleted, stale: false});
            }
          } else {
            buffer.stale = true;
          }
          render();
        }
      }
    }
    if (mode === "diff" && current) await loadDiff();
    renderBar();
  }

  function show(visible = root.hidden) {
    root.hidden = !visible;
    document.getElementById("editor-toggle")?.setAttribute("aria-pressed", String(visible));
    clearInterval(timer);
    timer = null;
    if (visible) {
      enqueue(refreshList).catch(fail);
      timer = setInterval(() => api.poll().catch(() => {}), pollInterval);
    }
  }

  function close() { show(false); }

  $("[data-view=text]").addEventListener("input", () => { syncBuffer(); renderBar(); renderList(); });
  $("[data-view=text]").addEventListener("keydown", (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key === "s") {
      event.preventDefault();
      api.save().catch(fail);
    }
  });
  $("[data-action=save]").addEventListener("click", () => api.save().catch(fail));
  $("[data-action=refresh]").addEventListener("click", () => api.poll().catch(fail));
  $("[data-action=close]").addEventListener("click", close);
  $("[data-action=keep-mine]").addEventListener("click", () => api.keepMine().catch(fail));
  $("[data-action=use-disk]").addEventListener("click", () => api.useDisk().catch(fail));
  $("[data-action=cancel]").addEventListener("click", () => api.cancel().catch(fail));
  $("[data-action=restore]").addEventListener("click", restore);
  $("[data-action=discard]").addEventListener("click", discardRecovered);
  $("[data-input=changed]").addEventListener("change", (event) => { filter.changedOnly = event.target.checked; renderList(); });
  $("[data-input=filter]").addEventListener("input", (event) => { filter.text = event.target.value; renderList(); });
  for (const tab of root.querySelectorAll("[data-mode]")) tab.addEventListener("click", () => view(tab.dataset.mode).catch(fail));

  api.ready = Promise.resolve();
  render();
  return api;
};
