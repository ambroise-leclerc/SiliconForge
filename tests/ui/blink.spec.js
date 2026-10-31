describe("Blink firmware", () => {
  const sf = () => window.siliconForge;
  const pinElement = () => document.querySelector('.pin[data-pin="PB5"]');

  // The emulation runs flat out, so the pin keeps toggling: observe a level, freeze the emulation (it may move on a
  // little before stopping), then check that the page shows exactly what the model holds.
  const settleOn = async (level) => {
    await sf().waitForPin("D13", level, 5000);
    await sf().pause();
    await sf().waitFor(() => String(sf().pin("D13").state) === pinElement().dataset.state, 5000);
  };

  it("toggles D13 and updates the board", async () => {
    await webFront.ready;
    await sf().waitFor((state) => state.mcu === "ATmega328P", 5000);
    await sf().setSpeed(0);
    await sf().run();
    await settleOn(sf().pinState.high);
    await sf().run();
    await settleOn(sf().pinState.low);
    await sf().run();
    await settleOn(sf().pinState.high);
  });
});
