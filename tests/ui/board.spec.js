// UI tests: Jasmine specs running in the SiliconForge page, against a live emulated MCU.
// They drive the firmware through window.siliconForge (ui/siliconforge.js), exactly as a user does through the page,
// and check both the MCU behaviour and what the page displays.
//
// Expected firmware: firmware/prebuilt/uart_echo.hex (see README.md in this directory).

describe("SiliconForge board page", () => {
  const sf = () => window.siliconForge;

  beforeAll(async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 5000);
  });

  beforeEach(async () => {
    await sf().reset();
    await sf().setSpeed(0);  // As fast as possible: timeouts below are wall-clock limits
    await sf().run();
  });

  it("shows the firmware greeting in the serial monitor", async () => {
    await sf().waitForSerial("ready\r\n", 3000);
    expect(document.getElementById("serial-output").textContent).toContain("ready");
  });

  it("echoes a line sent from the page in upper case", async () => {
    await sf().waitForSerial("ready", 3000);
    document.getElementById("serial-text").value = "silicon";
    document.getElementById("serial-form").requestSubmit();
    await sf().waitForSerial("SILICON\r\n", 3000);
  });

  it("lets a test drive an input pin and see it on the board", async () => {
    await sf().setPin("D2", 1);
    await sf().waitForPin("D2", sf().pinState.high, 2000);
    await sf().waitFor(() => document.querySelector('.pin[data-pin="PD2"]').dataset.state === "1", 2000);
    await sf().waitFor(() => document.querySelector('.bb-leg[data-pin="PD2"]').dataset.state === "1", 2000);
    await sf().waitFor(() => document.querySelector('.po-io[data-pin="PD2"]').dataset.state === "1", 2000);
    await sf().setPin("D2", null);
    await sf().waitForPin("D2", sf().pinState.floating, 2000);
  });

  it("shows USART0 transmit pin as an output once the transmitter is enabled", async () => {
    await sf().waitForSerial("ready", 3000);
    expect(sf().pin("D1").output).toBeTrue();
  });

  it("pulls D2 low while the breadboard push button is pressed", async () => {
    const button = document.querySelector('.bb-button[data-pin="D2"]');
    button.dispatchEvent(new PointerEvent("pointerdown", {bubbles: true}));
    await sf().waitForPin("D2", sf().pinState.low, 2000);
    button.dispatchEvent(new PointerEvent("pointerup", {bubbles: true}));
    await sf().waitForPin("D2", sf().pinState.floating, 2000);
  });

  it("applies the potentiometer voltage to A0", async () => {
    const pot = document.querySelector('.bb-pot[data-pin="A0"]');
    for (let i = 0; i < 25; ++i) pot.dispatchEvent(new WheelEvent("wheel", {deltaY: -1, cancelable: true}));
    expect(pot.querySelector(".bb-value").textContent).toBe("2.50 V");
  });

  it("lights the pinout level dot of a high pin", async () => {
    const dot = () => getComputedStyle(document.querySelector('.po-io[data-pin="PD2"] .po-level')).fill;
    await sf().setPin("D2", 0);
    await sf().waitFor(() => document.querySelector('.po-io[data-pin="PD2"]').dataset.state === "0", 2000);
    const low = dot();
    await sf().setPin("D2", 1);
    await sf().waitFor(() => document.querySelector('.po-io[data-pin="PD2"]').dataset.state === "1", 2000);
    expect(dot()).not.toBe(low);
    await sf().setPin("D2", null);
  });

  it("keeps the potentiometer and the A0 slider on the same voltage", async () => {
    const pot = document.querySelector('.bb-pot[data-pin="A0"]');
    const slider = document.querySelector('.pin[data-pin="PC0"] input[type="range"]');
    slider.value = "4";
    slider.dispatchEvent(new Event("input"));
    await sf().waitFor(() => pot.querySelector(".bb-value").textContent === "4.00 V", 2000);
    pot.dispatchEvent(new WheelEvent("wheel", {deltaY: -1, cancelable: true}));
    expect(pot.querySelector(".bb-value").textContent).toBe("4.10 V");
    await sf().waitFor(() => Number(slider.value).toFixed(2) === "4.10", 2000);
  });
});
