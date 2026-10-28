// Manual integration test: use a dedicated server; this replaces its MCU and firmware.
const {chromium} = require(process.env.SILICONFORGE_PLAYWRIGHT_MODULE || "playwright");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, "../..");

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE,
    args: ["--no-sandbox"],
  });
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(process.env.SILICONFORGE_URL || "http://127.0.0.1:9002");
    await page.waitForFunction(() => window.siliconForge?.state);
    await page.evaluate(() => siliconForge.selectMcu("atmega328p"));
    await page.waitForFunction(() => siliconForge.state.mcu === "ATmega328P");
    await page.waitForSelector("#breadboard svg");
    const chip = await page.evaluate(() => siliconForge.chip());
    assert.equal(chip.pins.length, 28);

    const description = JSON.parse(fs.readFileSync(path.join(root, "mcus/atmega328p.json"), "utf8"));
    description.name = "ATmega168";
    description.memories.flash.size = 16384;
    description.memories.sram.size = 1024;
    description.memories.eeprom.size = 512;
    description.registers.SPH.reset = 4;
    await page.setInputFiles("#mcu-file", {
      name: "atmega168.json",
      mimeType: "application/json",
      buffer: Buffer.from(JSON.stringify(description)),
    });
    await page.waitForFunction(() => siliconForge.state.mcu === "ATmega168");
    await page.waitForFunction(() => document.querySelector("#mcu-select").selectedOptions[0]?.text === "ATmega168");
    await page.waitForFunction(() => document.querySelector("#breadboard .bb-chip-name")?.textContent === "atmega168");
    await page.click("#tab-pinout");
    await page.waitForSelector('#pinout svg[aria-label="ATmega168 pinout"]');

    const bytes = Array.from(fs.readFileSync(path.join(root, "firmware/prebuilt/blink.hex")));
    await page.evaluate(async (bytes) => {
      await siliconForge.load("blink.hex", new Uint8Array(bytes));
      await siliconForge.setSpeed(10);
      await siliconForge.run();
    }, bytes);
    await page.waitForFunction(() => siliconForge.state.cycles > 20000000);
    const malformed = structuredClone(description);
    const adcIndex = malformed.peripherals.findIndex((peripheral) => peripheral.type === "avr.adc");
    assert.ok(adcIndex >= 0, "missing ADC in test description");
    malformed.peripherals[adcIndex].vector = "missing";
    await page.setInputFiles("#mcu-file", {
      name: "bad.json",
      mimeType: "application/json",
      buffer: Buffer.from(JSON.stringify(malformed)),
    });
    await page.waitForFunction((pointer) => document.querySelector("#mcu-error").textContent.includes(pointer), `/peripherals/${adcIndex}/vector`);
    assert.equal(await page.evaluate(() => siliconForge.state.mcu), "ATmega168");

    const bare = structuredClone(description);
    delete bare.breadboard;
    bare.name = "BareAVR";
    const labels = {PB0: "PB10", PB1: "PB2", PB2: "AVCC", PB3: "AREF"};
    for (const pin of bare.package.pins) {
      if (labels[pin.port]) pin.label = labels[pin.port];
    }
    // Fail the first chip request after replacement; a later frame must retry it.
    await page.evaluate(() => {
      const original = siliconForge.chip;
      window.chipAttempts = 0;
      siliconForge.chip = () => {
        if (++window.chipAttempts === 1) return Promise.reject(new Error("test: transient chip failure"));
        return original();
      };
    });
    await page.evaluate((text) => siliconForge.loadMcu(text), JSON.stringify(bare));
    await page.waitForSelector('#pinout svg[aria-label="BareAVR pinout"]');
    await page.waitForFunction(() => !document.querySelector("#breadboard svg"));
    await page.waitForFunction(() => document.querySelector("#mcu-select").selectedOptions[0]?.text === "BareAVR");
    assert.ok(await page.evaluate(() => window.chipAttempts >= 2));
    const pinLabels = await page.locator("#digital .pin .label").allTextContents();
    assert.ok(pinLabels.includes("AVCC") && pinLabels.includes("AREF"));
    assert.ok(pinLabels.indexOf("PB2") < pinLabels.indexOf("PB10"));
    const analogLabels = await page.locator("#analog .pin .label").allTextContents();
    assert.ok(analogLabels.every((label) => /^A\d+$/.test(label)));

    await page.selectOption("#mcu-select", "atmega328p");
    await page.waitForFunction(() => siliconForge.state.mcu === "ATmega328P");
    await page.waitForFunction(() => document.querySelector("#mcu-select").value === "atmega328p");
    await page.waitForFunction(() => document.querySelector("#breadboard .bb-chip-name")?.textContent === "atmega328p");
    assert.deepEqual(errors, []);
    if (process.env.SILICONFORGE_SCREENSHOT) {
      await page.screenshot({path: process.env.SILICONFORGE_SCREENSHOT, fullPage: true});
    }
    console.log("UI OK: upload, selection, views, transient chip retry, generic pin labels, firmware and validation errors");
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
