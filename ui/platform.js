// Names the platform on <html> before the first style is computed, so that style.css picks its font stacks for it.
// On Linux, each family absent from a stack, or each CSS keyword such as system-ui, makes fontconfig walk every
// installed font: the interface keeps to the generic families there, and names its native fonts only on Windows and macOS.
(() => {
  const name = (navigator.userAgentData?.platform || navigator.platform || "").toLowerCase();
  const platform = name.startsWith("win") ? "windows" : name.startsWith("mac") ? "macos" : "other";
  document.documentElement.dataset.platform = platform;
})();
