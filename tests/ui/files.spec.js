// Files and errors in the native window: firmware and MCU description chosen through the page's <input type="file">,
// the typed errors of the sf.* functions, and strings larger than 64 KiB crossing the bridge (WebFront 0.9).
//
// The system file dialog itself is not driven: WebFront offers no consumer API to answer it (its test injections are
// internal to each frontend). The spec hands the input a File the way the dialog does, through `input.files`, and
// dispatches the `change` event, so the page's own handlers run in the real window. window.sfFixtures is generated
// by tests/CMakeLists.txt from firmware/prebuilt/blink.hex and mcus/atmega328p.json.
// The application is one shared simulation: run the specs in the order written.
jasmine.getEnv().configure({random: false});

describe("Files and errors in the window", () => {
  const sf = () => window.siliconForge;
  const fixtures = window.sfFixtures;
  const element = (id) => document.getElementById(id);

  /** Gives `input` a file as the chooser would, and notifies the page. */
  const choose = (id, name, content, type = "") => {
    const transfer = new DataTransfer();
    transfer.items.add(new File([content], name, {type}));
    element(id).files = transfer.files;
    element(id).dispatchEvent(new Event("change", {bubbles: true}));
  };

  /** Resolves when `predicate()` holds, polling the page for at most `timeoutMs`. */
  const until = async (predicate, what, timeoutMs = 5000) => {
    const deadline = performance.now() + timeoutMs;
    while (!predicate()) {
      if (performance.now() > deadline) throw new Error(`timeout waiting for ${what}`);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  };

  const description = (changes) => JSON.stringify({...fixtures.mcu, ...changes});
  const rejection = async (promise) => {
    try { await promise; } catch (error) { return error; }
    throw new Error("the call did not reject");
  };

  let alerted;
  const nativeAlert = window.alert;

  beforeEach(async () => {
    alerted = 0;
    window.alert = () => { alerted += 1; };
    await sf().selectMcu("atmega328p");
    await sf().pause();
    await sf().waitFor((s) => s.mcuId === "atmega328p" && !s.running);
    element("firmware-error").hidden = true;
    element("mcu-error").hidden = true;
  });

  afterEach(() => { window.alert = nativeAlert; });

  it("lists the catalog as an array", async () => {
    const names = await sf().mcus();
    expect(Array.isArray(names)).toBe(true);
    expect(names).toContain("atmega328p");
  });

  it("loads a firmware chosen with the file input and runs it", async () => {
    choose("firmware-file", "fixture.hex", fixtures.hex);
    await sf().waitFor((s) => s.firmware === "fixture.hex" && s.running);
    expect(element("firmware-error").hidden).toBe(true);
  });

  it("reports an invalid firmware in the page, not in an alert box", async () => {
    choose("firmware-file", "broken.hex", ":10000000ZZ\n");
    await until(() => !element("firmware-error").hidden, "the firmware error");
    expect(element("firmware-error").textContent).toContain("Cannot load broken.hex");
    expect(element("firmware-error").textContent).toContain("(line 1)");
    expect(alerted).toBe(0);
    choose("firmware-file", "fixture.hex", fixtures.hex);
    await until(() => element("firmware-error").hidden, "the error to clear after a good load");
  });

  it("rejects firmware with a typed error", async () => {
    const error = await rejection(sf().load("broken.hex", new TextEncoder().encode(":10000000ZZ\n")));
    expect(error.code).toBe("siliconforge.invalidFirmware");
    expect(error.message).toContain("(line 1)");
  });

  it("loads an MCU description chosen with the file input", async () => {
    choose("mcu-file", "fixture.json", description({name: "Fixture MCU"}), "application/json");
    const state = await sf().waitFor((s) => s.mcu === "Fixture MCU");
    expect(state.mcuId).toBeNull();
    expect(element("mcu-error").hidden).toBe(true);
  });

  it("reports an invalid MCU description with its JSON path", async () => {
    choose("mcu-file", "broken.json", description({clockHz: "fast"}), "application/json");
    await until(() => !element("mcu-error").hidden, "the MCU error");
    expect(element("mcu-error").textContent).toContain("clockHz");
    expect(sf().state.mcuId).toBe("atmega328p");
  });

  it("rejects MCU loading and selection with typed errors", async () => {
    const invalid = await rejection(sf().loadMcu(description({clockHz: "fast"})));
    expect(invalid.code).toBe("siliconforge.invalidMcu");
    expect(invalid.message).toContain("clockHz");
    const unknown = await rejection(sf().selectMcu("no-such-mcu"));
    expect(unknown.code).toBe("siliconforge.unknownMcu");
    expect(unknown.message).toBe("Unknown MCU: no-such-mcu");
  });

  it("carries descriptions and chips larger than 64 KiB", async () => {
    // The name travels in the description (browser -> C++), in the chip (C++ -> browser) and in every state frame.
    // It has word breaks: WebKit lays out a 100 KiB word very slowly, which would measure the page, not the bridge.
    const name = `Big ${"word ".repeat(20000)}`;
    const text = description({name});
    expect(text.length).toBeGreaterThan(64 * 1024);
    await sf().loadMcu(text);
    const state = await sf().waitFor((s) => s.mcu === name);
    expect(state.mcuId).toBeNull();
    const chip = await sf().chip();
    expect(JSON.stringify(chip).length).toBeGreaterThan(64 * 1024);
  });
});
