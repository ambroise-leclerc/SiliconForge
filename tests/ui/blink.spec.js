describe("Blink firmware", () => {
  const sf = () => window.siliconForge;

  it("toggles D13 and updates the board", async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 5000);
    await sf().setSpeed(0);
    await sf().run();
    await sf().waitForPin("D13", sf().pinState.high, 5000);
    await sf().waitFor(() => document.querySelector('.pin[data-pin="PB5"]').dataset.state === "1", 5000);
    await sf().waitForPin("D13", sf().pinState.low, 5000);
  });
});
