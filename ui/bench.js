// Bench: runs the scenarios of the project (ADR 0010) and examines their evidence in the window. The engine is the one
// of siliconforge bench, and a run keeps its report.json and trace.vcd where the command line does: a run of the agent
// opens here, and a run started here can be compared with it, since the same inputs give the same report.
"use strict";

window.SiliconForgeBench = (root, host) => {
  const $ = (selector) => root.querySelector(selector);
  const element = (tag, text, className) => {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = text;
    if (className) node.className = className;
    return node;
  };
  const format = (value) => (typeof value === "number" ? Number(value.toPrecision(6)).toString() : String(value));

  let current = null;  // {run, directory, report, trace, previous?, identical?, exitCode?}

  /** Every leaf of a JSON value by its JSON Pointer, so that two reports compare path by path. */
  function leaves(value, path = "", into = new Map()) {
    if (value !== null && typeof value === "object") {
      for (const [key, child] of Object.entries(value)) leaves(child, `${path}/${key}`, into);
    } else {
      into.set(path, value);
    }
    return into;
  }

  /** What differs between two reports: [{path, before, after}], empty when they say the same. */
  function differences(before, after) {
    const a = leaves(before);
    const b = leaves(after);
    const paths = [...new Set([...a.keys(), ...b.keys()])];
    return paths.filter((path) => a.get(path) !== b.get(path)).map((path) => ({path, before: a.get(path), after: b.get(path)}));
  }

  const parse = (json) => JSON.parse(json);

  const api = {
    get result() { return current; },
    show,
    differences,
    list: async () => parse(await host.call("sf.bench.list")),
    /** Opens the evidence kept for a run, whoever ran it. */
    open: async (name) => show(parse(await host.call("sf.bench.open", name))),
    /** Runs a scenario of the project and compares it with the run it replaces. */
    run: async (file) => show(parse(await host.call("sf.bench.run", file))),
    /** The trace of the shown run as VCD text, or null. */
    trace: () => current?.trace ?? null,
  };

  /** Shows a result as `run` and `open` return it. */
  function show(result) {
    current = result;
    render();
    return result;
  }

  function fail(error) {
    const message = $("[data-readout=error]");
    message.textContent = error ? `${error.code ?? ""} ${error.message ?? error}`.trim() : "";
    message.hidden = !error;
  }

  function render() {
    const report = current?.report;
    root.dataset.verdict = report?.verdict ?? "none";
    $("[data-readout=title]").textContent = report ? `${report.scenario} — ${current.directory}` : "";
    const verdict = $("[data-readout=verdict]");
    verdict.textContent = report ? report.verdict : "";
    verdict.dataset.verdict = report?.verdict ?? "";
    renderComparison();
    $("[data-action=save-trace]").disabled = !current?.trace;
    $("[data-readout=trace]").textContent = report?.trace && current.trace
      ? `${report.trace.file}: ${current.trace.length} bytes, timescale ${report.trace.timescale}` : report ? "no trace" : "";
    const problem = $("[data-readout=problem]");
    problem.hidden = !report?.problem;
    problem.textContent = report?.problem ? `Incompatible (${report.problem.cause}) ${report.problem.file}${report.problem.path}: ${report.problem.message}` : "";
    const inputs = $("[data-list=inputs]");
    inputs.replaceChildren(...(report?.inputs ?? []).map((input) => {
      const item = element("li");
      item.append(element("span", input.role), element("span", input.path, "muted"), element("code", input.sha256.slice(0, 12)));
      item.title = input.sha256;
      return item;
    }));
    const steps = $("[data-list=steps]");
    steps.replaceChildren(...(report?.steps ?? []).map((step) => {
      const row = element("tr");
      row.dataset.outcome = step.outcome;
      row.append(element("td", step.index), element("td", step.outcome, "outcome"), element("td", step.kind),
        element("td", [step.label, step.detail].filter(Boolean).join(" — ")));
      return row;
    }));
    const measurements = $("[data-list=measurements]");
    measurements.replaceChildren(...(report?.measurements ?? []).map((m) => {
      const row = element("tr");
      row.dataset.verdict = m.verdict;
      const expected = m.expected ? `${format(m.expected.min)} to ${format(m.expected.max)} ${m.unit}` : "informative";
      row.append(element("td", m.verdict, "outcome"), element("td", `${m.id} (${m.pin} ${m.quantity})`),
        element("td", m.value === null ? "not measurable" : `${format(m.value)} ${m.unit}`), element("td", expected, "muted"));
      if (m.detail) row.title = m.detail;
      return row;
    }));
    $("[data-readout=serial]").textContent = (report?.serial ?? []).map((port) => port.text).join("");
    root.querySelector("[data-section=details]").hidden = !report;
  }

  function renderComparison() {
    const out = $("[data-readout=comparison]");
    const details = $("[data-section=changes]");
    out.dataset.state = "";
    details.hidden = true;
    details.open = false;
    if (!current || current.identical === undefined) { out.textContent = ""; return; }
    if (current.previous === null) {
      out.textContent = "No earlier run of this scenario to compare with.";
    } else if (current.identical) {
      out.textContent = "Same report as the earlier run: same inputs, same engine, same result.";
      out.dataset.state = "same";
    } else {
      const changes = differences(current.previous, current.report);
      out.textContent = `Differs from the earlier run at ${changes.length} place(s).`;
      out.dataset.state = "different";
      // Every path is listed: the summary above is a count, not a sample.
      $("[data-list=changes]").replaceChildren(...changes.map((change) => {
        const item = element("li");
        item.append(element("code", change.path), element("span", `${format(change.before)} → ${format(change.after)}`, "muted"));
        return item;
      }));
      details.hidden = false;
    }
  }

  async function refresh(select) {
    const {scenarios, runs} = await api.list();
    $("[data-select=scenario]").replaceChildren(...scenarios.map((name) => new Option(name, name)));
    const runSelect = $("[data-select=run]");
    runSelect.replaceChildren(...runs.map((run) => {
      const option = new Option(`${run.name} — ${run.verdict}`, run.name);
      option.title = run.scenario;
      return option;
    }));
    if (select) runSelect.value = select;
    $("[data-action=run]").disabled = scenarios.length === 0;
    $("[data-action=open]").disabled = runs.length === 0;
    $("[data-readout=empty]").hidden = scenarios.length > 0 || runs.length > 0;
  }
  api.refresh = refresh;

  async function guarded(action) {
    fail(null);
    const buttons = root.querySelectorAll("button[data-action=run], button[data-action=open]");
    buttons.forEach((button) => { button.disabled = true; });
    try {
      await action();
    } catch (error) {
      fail(error);
    } finally {
      await refresh(current?.run).catch(fail);
    }
  }

  root.addEventListener("click", (event) => {
    const action = event.target.closest("[data-action]")?.dataset.action;
    if (action === "run") guarded(() => api.run($("[data-select=scenario]").value));
    else if (action === "open") guarded(() => api.open($("[data-select=run]").value));
    else if (action === "refresh") guarded(async () => {});
    else if (action === "save-trace" && current?.trace) {
      const link = document.createElement("a");
      link.href = URL.createObjectURL(new Blob([current.trace], {type: "text/plain"}));
      link.download = `${current.run}.vcd`;
      document.body.append(link);
      link.click();
      link.remove();
      setTimeout(() => URL.revokeObjectURL(link.href), 10000);
    }
  });

  render();
  api.ready = host.ready.then(() => refresh()).catch(fail);
  return api;
};
