// ATmega328P description for the board views: DIP-28 pinout with the functions of each pin, and the demonstration
// circuit of the breadboard view. Another chip adds its own file registering under its name (state.mcu).
"use strict";

window.SiliconForgeChips = window.SiliconForgeChips || {};

window.SiliconForgeChips["ATmega328P"] = {
  name: "ATmega328P",
  // Physical pins in package order. Labels go from the chip outwards, one slot each (null = empty slot):
  // port, pin change interrupt, function, Arduino pin, PWM, other function. "text:kind" overrides the slot's colour.
  pins: [
    {n: 1, labels: ["PC6", "PCINT14", "RESET:control"]},
    {n: 2, labels: ["PD0", "PCINT16", "RXD:serial", "D0"]},
    {n: 3, labels: ["PD1", "PCINT17", "TXD:serial", "D1"]},
    {n: 4, labels: ["PD2", "PCINT18", "INT0", "D2"]},
    {n: 5, labels: ["PD3", "PCINT19", "INT1", "D3", "PWM", "OC2B"]},
    {n: 6, labels: ["PD4", "PCINT20", "XCK", "D4", null, "T0"]},
    {n: 7, labels: ["VCC:power"]},
    {n: 8, labels: ["GND:ground"]},
    {n: 9, labels: ["PB6", "PCINT6", "XTAL1", null, null, "TOSC1"]},
    {n: 10, labels: ["PB7", "PCINT7", "XTAL2", null, null, "TOSC2"]},
    {n: 11, labels: ["PD5", "PCINT21", "T1", "D5", "PWM", "OC0B"]},
    {n: 12, labels: ["PD6", "PCINT22", "AIN0:analog", "D6", "PWM", "OC0A"]},
    {n: 13, labels: ["PD7", "PCINT23", "AIN1:analog", "D7"]},
    {n: 14, labels: ["PB0", "PCINT0", "CLKO", "D8", null, "ICP1"]},
    {n: 15, labels: ["PB1", "PCINT1", "OC1A", "D9", "PWM"]},
    {n: 16, labels: ["PB2", "PCINT2", "OC1B", "D10", "PWM", "SS:serial"]},
    {n: 17, labels: ["PB3", "PCINT3", "OC2A", "D11", "PWM", "MOSI:serial"]},
    {n: 18, labels: ["PB4", "PCINT4", null, "D12", null, "MISO:serial"]},
    {n: 19, labels: ["PB5", "PCINT5", null, "D13", null, "SCK:serial"]},
    {n: 20, labels: ["AVCC:power"]},
    {n: 21, labels: ["AREF:analog"]},
    {n: 22, labels: ["GND:ground"]},
    {n: 23, labels: ["PC0", "PCINT8", "ADC0:analog", "A0:analog"]},
    {n: 24, labels: ["PC1", "PCINT9", "ADC1:analog", "A1:analog"]},
    {n: 25, labels: ["PC2", "PCINT10", "ADC2:analog", "A2:analog"]},
    {n: 26, labels: ["PC3", "PCINT11", "ADC3:analog", "A3:analog"]},
    {n: 27, labels: ["PC4", "PCINT12", "ADC4:analog", "A4:analog", null, "SDA:serial"]},
    {n: 28, labels: ["PC5", "PCINT13", "ADC5:analog", "A5:analog", null, "SCL:serial"]},
  ],
  slotKinds: ["port", "digital", "function", "ide", "pwm", "function"],

  // Breadboard: the chip straddles the channel with pin 1 in hole e6. Holes are "<row><column>" (rows a-e below the
  // channel, f-j above), rails are "T+", "T-" (top) and "B+", "B-" (bottom) followed by a column.
  breadboard: {
    columns: 30,
    chipColumn: 6,
    components: [
      // Power
      {type: "wire", color: "red", from: "b12", to: "B+12"},       // VCC
      {type: "wire", color: "black", from: "a13", to: "B-13"},     // GND
      {type: "wire", color: "red", from: "i14", to: "T+14"},       // AVCC
      {type: "wire", color: "black", from: "j12", to: "T-12"},     // GND
      {type: "crystal", from: "d14", to: "d15", label: "16 MHz"},  // XTAL1, XTAL2
      // D13: the "L" LED of the Arduino Uno
      {type: "resistor", from: "h15", to: "h22", label: "220 Ω"},
      {type: "led", color: "red", pin: "D13", anode: "f22", cathode: "f23", offset: [16, -40]},
      {type: "wire", color: "black", from: "j23", to: "T-23"},
      // D6 (OC0A): PWM brightness
      {type: "resistor", from: "c17", to: "c24", label: "220 Ω"},
      {type: "led", color: "green", pin: "D6", anode: "e24", cathode: "e25", offset: [16, -24]},
      {type: "wire", color: "black", from: "a25", to: "B-25"},
      // D2: push button to ground, with the internal pull-up
      {type: "wire", color: "yellow", from: "b9", to: "b4"},
      {type: "button", pin: "D2", from: "c4", to: "c2"},
      {type: "wire", color: "black", from: "a2", to: "B-2"},
      // A0: potentiometer between GND and 5 V
      {type: "potentiometer", pin: "A0", legs: ["g2", "g3", "g4"], offset: [0, -27]},
      {type: "wire", color: "black", from: "j2", to: "T-2"},
      {type: "wire", color: "red", from: "j4", to: "T+4"},
      {type: "wire", color: "blue", from: "f3", to: "g11"},
    ],
  },
};
