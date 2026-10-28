#include <siliconforge/arch/avr/Device.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <set>

namespace siliconforge::avr {
namespace {
using Json = nlohmann::json;
AvrCoreConfig config(const Json& d) {
    const auto& m       = d.at("memories");
    const auto& c       = d.at("cpu");
    const auto  address = [&](const char* key) {
        if (!c.contains(key))
            return Address{0};
        return d.at("registers").at(c.at(key).get<std::string>()).at("address").get<Address>();
    };
    return {.flashBytes      = m.at("flash").at("size"),
            .ioEnd           = m.at("io").at("start").get<Address>() + m.at("io").at("size").get<Address>() - 1,
            .ramEnd          = m.at("sram").at("start").get<Address>() + m.at("sram").at("size").get<Address>() - 1,
            .vectorWords     = c.at("vectorWords"),
            .flashPageBytes  = m.at("flash").at("pageSize"),
            .spmcsr          = address("spmcsr"),
            .smcr            = address("smcr"),
            .spl             = address("spl"),
            .sph             = address("sph"),
            .sreg            = address("sreg"),
            .pcBytes         = c.at("pcBytes"),
            .interruptCycles = c.at("interruptCycles"),
            .wakeUpCycles    = c.at("wakeUpCycles")};
}
}  // namespace

Device::Device(Json d, u64 clockOverride)
    : description(std::make_unique<Json>(std::move(d))),
      coreConfig(config(*description)),
      deviceName(description->at("name")),
      clock(clockOverride ? clockOverride : description->at("clockHz").get<u64>()),
      supplyVoltage(description->at("supplyVoltage")),
      data(coreConfig.ioEnd, coreConfig.ramEnd),
      core(coreConfig, data, scheduler, interrupts, debug),
      eeprom(description->at("memories").at("eeprom").at("size"), 0xFF) {
    auto view    = description->at("package");
    view["name"] = deviceName;
    if (description->contains("breadboard"))
        view["breadboard"] = description->at("breadboard");
    chip = view.dump();
    for (const auto& p : description->at("peripherals"))
        if (p.at("type") == "avr.gpio")
            for (const auto& n : p.at("pins")) {
                std::string label;
                for (const auto& physical : description->at("package").at("pins"))
                    if (physical.at("port") == n)
                        label = physical.at("label");
                pinList.emplace_back(n.get<std::string>(), label, &core.cycles);
            }
    for (const auto& p : description->at("peripherals"))
        if (p.at("type") == "avr.usart")
            serialList.emplace_back(p.at("name").get<std::string>());
    core.setTiming(description->at("timing"));
    // Named architecture factories. GPIO is wired first so other peripherals may refer to it, regardless of JSON order.
    using Factory = void (Device::*)(const Json&);
    static const std::map<std::string, Factory> factories{
        {  "avr.gpio",   &Device::makeGpio},
        {"avr.timer8",  &Device::makeTimer},
        { "avr.usart",  &Device::makeUsart},
        {"avr.eeprom", &Device::makeEeprom},
        {   "avr.adc",    &Device::makeAdc}
    };
    std::set<Address> gpioControls;
    for (const auto& p : description->at("peripherals")) {
        if (p.at("type") != "avr.gpio")
            continue;
        makeGpio(p);
        gpioControls.insert(description->at("registers").at(p.at("mcucr").get<std::string>()).at("address").get<Address>());
    }
    for (const auto mcucr : gpioControls) {
        data.map(mcucr, {nullptr, [this, mcucr](u8 v, u8 mask) {
                             auto& stored = data.raw(mcucr);
                             stored       = static_cast<u8>((stored & ~mask) | (v & mask));
                             for (auto& [name, gpio] : ports)
                                 gpio->refresh();
                         }});
    }
    for (const auto& p : description->at("peripherals"))
        if (p.at("type") != "avr.gpio")
            (this->*factories.at(p.at("type")))(p);
    reset();
}

Device::~Device() = default;

Address Device::reg(const Json& p, std::string_view key) const {
    return description->at("registers").at(p.at("registers").at(key).get<std::string>()).at("address");
}
unsigned Device::vector(const Json& p, std::string_view key) const {
    return description->at("vectors").at(p.at(key).get<std::string>());
}
void Device::makeGpio(const Json& p) {
    std::array<Pin*, 8> pins{};
    for (std::size_t i = 0; i < p.at("pins").size(); ++i)
        pins[i] = pin(p.at("pins")[i].get<std::string>());
    const auto mcucr = description->at("registers").at(p.at("mcucr").get<std::string>()).at("address").get<Address>();
    auto       port  = std::make_unique<GpioPort>(data, GpioPort::Registers{reg(p, "pin"), reg(p, "ddr"), reg(p, "port")}, pins, mcucr);
    resetters.emplace_back([ptr = port.get()] {
        ptr->reset(true);
    });
    ports.emplace(p.at("name"), std::move(port));
}
void Device::makeTimer(const Json& p) {
    const auto output = [&](unsigned i) {
        const auto& o = p.at("outputs")[i];
        return Timer8::OutputPin{ports.at(o.at("port")).get(), o.at("bit")};
    };
    const auto& v     = p.at("vectors");
    auto        timer = std::make_unique<Timer8>(
        data,
        scheduler,
        interrupts,
        core.cycles,
        Timer8::Registers{reg(p, "tccrA"), reg(p, "tccrB"), reg(p, "tcnt"), reg(p, "ocrA"), reg(p, "ocrB"), reg(p, "timsk"), reg(p, "tifr")},
        Timer8::Vectors{vector(v, "compareA"), vector(v, "compareB"), vector(v, "overflow")},
        p.at("prescalers").get<std::array<u16, 8>>(),
        output(0),
        output(1));
    resetters.emplace_back([ptr = timer.get()] {
        ptr->reset(true);
    });
    timers.push_back(std::move(timer));
}
void Device::makeUsart(const Json& p) {
    const auto& v     = p.at("vectors");
    auto        usart = std::make_unique<Usart>(data,
                                         scheduler,
                                         interrupts,
                                         core.cycles,
                                         serialList[usarts.size()],
                                         Usart::Registers{reg(p, "udr"), reg(p, "ucsrA"), reg(p, "ucsrB"), reg(p, "ucsrC"), reg(p, "ubrrL"), reg(p, "ubrrH")},
                                         Usart::Vectors{vector(v, "rxComplete"), vector(v, "dataEmpty"), vector(v, "txComplete")},
                                         Usart::Pins{ports.at(p.at("port")).get(), p.at("rx"), p.at("tx")},
                                         p.at("clockDividers").get<std::array<u16, 2>>(),
                                         p.at("receiveFifoDepth"));
    resetters.emplace_back([ptr = usart.get()] {
        ptr->reset(true);
    });
    usarts.push_back(std::move(usart));
}
void Device::makeEeprom(const Json& p) {
    const auto control = reg(p, "control"), valueReg = reg(p, "data"), low = reg(p, "addressLow"), high = reg(p, "addressHigh");
    const auto irq         = vector(p, "vector");
    const auto writeCycles = clock * p.at("writeTimeNs").get<u64>() / 1'000'000'000;
    const auto readCycles  = p.at("readCycles").get<unsigned>();
    const auto update      = [this, control, irq] {
        const auto c = data.raw(control);
        interrupts.set(irq, (c & 8) && !(c & 2));
    };
    data.map(control, {nullptr, [=, this](u8 v, u8 mask) {
                           auto& stored       = data.raw(control);
                           v                  = static_cast<u8>((stored & ~mask) | (v & mask));
                           const auto address = static_cast<std::size_t>(data.raw(low) | data.raw(high) << 8) % eeprom.size();
                           if ((v & 2) && (stored & 4) && !(stored & 2)) {
                               const auto mode = (v >> 4) & 3;
                               if (mode == 0)
                                   eeprom[address] = data.raw(valueReg);
                               else if (mode == 1)
                                   eeprom[address] = 0xFF;
                               else if (mode == 2)
                                   eeprom[address] &= data.raw(valueReg);
                               scheduler.schedule(core.cycles + writeCycles, [=, this](Cycles) {
                                   data.raw(control) &= static_cast<u8>(~2);
                                   update();
                               });
                               v &= static_cast<u8>(~4);
                           } else if (!(stored & 2))
                               v &= static_cast<u8>(~2);
                           if (v & 1) {
                               data.raw(valueReg) = eeprom[address];
                               core.cycles       += readCycles;
                               v                 &= static_cast<u8>(~1);
                           }
                           stored = v;
                           update();
                       }});
    resetters.emplace_back(update);
}
void Device::makeAdc(const Json& p) {
    struct State {
        bool               first{true};
        Scheduler::EventId event{0};
    };
    auto       state   = std::make_shared<State>();
    const auto control = reg(p, "control"), mux = reg(p, "mux"), low = reg(p, "low"), high = reg(p, "high");
    const auto irq    = vector(p, "vector");
    const auto update = [this, control, irq] {
        const auto c = data.raw(control);
        interrupts.set(irq, (c & 0x10) && (c & 8));
    };
    const auto        div    = p.at("prescalers").get<std::array<unsigned, 8>>();
    const unsigned    normal = p.at("conversionCycles"), first = p.at("firstConversionCycles"), bandgap = p.at("bandgapChannel");
    const bool        modelFirst   = p.at("modelFirstConversion");
    const double      bandgapVolts = p.at("bandgapVoltage");
    std::vector<Pin*> channels;
    for (const auto& n : p.at("channels"))
        channels.push_back(pin(n.get<std::string>()));
    data.map(control,
             {nullptr, [=, this](u8 v, u8 mask) {
                  auto& stored = data.raw(control);
                  if (v & mask & 0x10)
                      stored &= static_cast<u8>(~0x10);
                  const auto keep = static_cast<u8>(mask & ~0x10);
                  stored          = static_cast<u8>((stored & ~keep) | (v & keep));
                  if (!(stored & 0x80)) {
                      state->first = true;
                      if (state->event)
                          scheduler.cancel(std::exchange(state->event, 0));
                      stored &= static_cast<u8>(~0x40);
                  }
                  if ((stored & 0xC0) == 0xC0 && !state->event) {
                      const auto duration = (modelFirst && state->first ? first : normal) * div[stored & 7];
                      state->first        = false;
                      state->event        = scheduler.schedule(core.cycles + duration, [=, this](Cycles) {
                          state->event         = 0;
                          const auto   channel = static_cast<unsigned>(data.raw(mux) & 15);
                          const double volts  = channel < channels.size() ? channels[channel]->voltage(supplyVoltage) : channel == bandgap ? bandgapVolts : 0.0;
                          const auto   result = static_cast<u16>(std::clamp(volts / supplyVoltage * 1024.0, 0.0, 1023.0));
                          if (data.raw(mux) & 0x20) {
                              data.raw(high) = static_cast<u8>(result >> 2);
                              data.raw(low)  = static_cast<u8>(result << 6);
                          } else {
                              data.raw(high) = static_cast<u8>(result >> 8);
                              data.raw(low)  = static_cast<u8>(result);
                          }
                          data.raw(control) = static_cast<u8>((data.raw(control) & ~0x40) | 0x10);
                          update();
                      });
                  }
                  update();
              }});
    interrupts.onAcknowledge(irq, [=, this] {
        data.raw(control) &= static_cast<u8>(~0x10);
        update();
    });
    resetters.emplace_back([=] {
        state->event = 0;
        state->first = true;
        update();
    });
}
void Device::load(const ProgramImage& image) {
    for (const auto& segment : image.segments) {
        const auto limit = segment.space == MemorySpace::Program  ? core.config().flashBytes
                           : segment.space == MemorySpace::Eeprom ? eeprom.size()
                                                                  : data.ramEndAddress() + 1;
        if (segment.address > limit || segment.bytes.size() > limit - segment.address)
            throw std::out_of_range("Firmware segment exceeds MCU memory");
    }
    core.eraseFlash();
    std::ranges::fill(eeprom, u8{0xFF});
    for (const auto& segment : image.segments) {
        if (segment.space == MemorySpace::Program)
            core.loadFlash(segment.address, segment.bytes);
        else if (segment.space == MemorySpace::Eeprom)
            std::ranges::copy(segment.bytes, eeprom.begin() + segment.address);
    }
    symbols = image.symbols;
    reset();
}
void Device::reset() {
    scheduler.clear();
    interrupts.clear();
    data.clear();
    core.reset();
    for (const auto& r : description->at("registers"))
        data.raw(r.at("address")) = r.at("reset");
    // Core registers have live read/write handlers; their reset values also come from the description.
    const auto& c = description->at("cpu");
    for (const char* key : {"spl", "sph", "sreg"}) {
        const auto& r = description->at("registers").at(c.at(key).get<std::string>());
        data.write(r.at("address"), r.at("reset"));
    }
    for (auto& port : serialList)
        port.reset();
    for (auto& reset : resetters)
        reset();
}
std::vector<RegisterValue> Device::registers() const {
    std::vector<RegisterValue> result;
    result.reserve(40);
    result.push_back({"PC", core.pc * 2, 16});
    result.push_back({"SP", core.sp, 16});
    result.push_back({"SREG", core.sreg, 8});
    result.push_back({"X", core.pair(26), 16});
    result.push_back({"Y", core.pair(28), 16});
    result.push_back({"Z", core.pair(30), 16});
    for (unsigned i = 0; i < 32; ++i)
        result.push_back({std::format("R{}", i), core.r(i), 8});
    return result;
}

std::vector<u8> Device::readMemory(MemorySpace space, Address address, std::size_t size) const {
    std::vector<u8> bytes;
    bytes.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const auto at = static_cast<Address>(address + i);
        switch (space) {
            case MemorySpace::Program:
                bytes.push_back(core.flashByte(at));
                break;
            case MemorySpace::Data:
                bytes.push_back(data.peek(at));
                break;
            case MemorySpace::Eeprom:
                bytes.push_back(at < eeprom.size() ? eeprom[at] : 0xFF);
                break;
        }
    }
    return bytes;
}

std::string Device::symbolAt(Address address) const {
    auto it = symbols.upper_bound(address);
    if (it == symbols.begin())
        return {};
    --it;
    return address == it->first ? it->second : std::format("{}+{:#x}", it->second, address - it->first);
}

}  // namespace siliconforge::avr
