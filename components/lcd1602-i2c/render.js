// LCD1602 behind an I2C backpack: drawn as the LCD1602, its plate wired to the four pins of the backpack. The rendering
// module of lcd1602 is loaded and shared; the loader waits for the promise this module registers.
"use strict";

SiliconForge.components.define("lcd1602-i2c", SiliconForge.components.load("lcd1602"));
