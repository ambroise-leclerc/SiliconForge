#include "EmbeddedDescriptions.hpp"
#include <nlohmann/json-schema.hpp>
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <algorithm>
#include <bit>
#include <fstream>
#include <set>

namespace siliconforge::mcu {
namespace {
using Json = nlohmann::json;
std::string child(std::string path, std::string_view key) {
    return (Json::json_pointer{path} / std::string(key)).to_string();
}
struct Errors : nlohmann::json_schema::error_handler {
    std::optional<LoadError> first;
    void                     error(const Json::json_pointer& pointer, const Json&, const std::string& message) override {
        if (first)
            return;
        auto              path   = pointer.to_string();
        const std::string prefix = "required property '";
        if (message.starts_with(prefix))
            path = child(path, message.substr(prefix.size(), message.find("'", prefix.size()) - prefix.size()));
        first = LoadError{path, message};
    }
};
void fail(std::string path, std::string message) {
    throw LoadError{std::move(path), std::move(message)};
}
}  // namespace

std::expected<void, LoadError> validate(const Json& d) {
    try {
        if (d.contains("peripherals") && d.at("peripherals").is_array())
            for (std::size_t i = 0; i < d.at("peripherals").size(); ++i) {
                const auto& p = d.at("peripherals")[i];
                if (!p.is_object() || !p.contains("type") || !p.at("type").is_string())
                    continue;
                const std::string type = p.at("type");
                if (type != "avr.gpio" && type != "avr.timer8" && type != "avr.usart" && type != "avr.adc" && type != "avr.eeprom")
                    fail("/peripherals/" + std::to_string(i) + "/type", "Unknown peripheral type: " + type);
            }
        // The same embedded Draft-07 schema is used by the runtime and the CTest validation test.
        static const nlohmann::json_schema::json_validator validator{Json::parse(embedded::schema)};
        Errors                                             errors;
        validator.validate(d, errors);
        if (errors.first)
            return std::unexpected(*errors.first);
        const auto& m = d.at("memories");
        for (const auto* n : {"flash", "eeprom"}) {
            const auto size = m.at(n).at("size").get<unsigned>();
            if (!std::has_single_bit(size))
                fail(std::string("/memories/") + n + "/size", "Size must be a power of two");
            if (m.at(n).at("start") != 0)
                fail(std::string("/memories/") + n + "/start", "This AVR memory must start at zero");
        }
        const auto flashSize = m.at("flash").at("size").get<unsigned>();
        const auto page      = m.at("flash").at("pageSize").get<unsigned>();
        if (!std::has_single_bit(page) || page > flashSize)
            fail("/memories/flash/pageSize", "Page size must be a power of two within flash");
        if (flashSize < 2)
            fail("/memories/flash/size", "Flash must contain at least one instruction word");
        if (m.at("registers").at("start") != 0 || m.at("registers").at("size") != 32)
            fail("/memories/registers", "AVR requires 32 general-purpose registers at zero");
        const auto ioStart = m.at("io").at("start").get<unsigned>(), ioSize = m.at("io").at("size").get<unsigned>();
        const auto ramStart = m.at("sram").at("start").get<unsigned>(), ramSize = m.at("sram").at("size").get<unsigned>();
        if (ioStart != 32 || ioStart + ioSize != ramStart)
            fail("/memories/sram/start", "SRAM must immediately follow AVR I/O space starting at 32");
        if (ramStart + ramSize > 65536)
            fail("/memories/sram/size", "AVR data space exceeds 16-bit addressing");
        if (d.at("cpu").at("pcBytes") != 2 && d.at("cpu").at("pcBytes") != 3)
            fail("/cpu/pcBytes", "Only two- and three-byte program counters are supported");
        std::set<unsigned> addresses, vectorNumbers, physicalNumbers;
        for (const auto& [name, r] : d.at("registers").items()) {
            const auto path = child("/registers", name) + "/address";
            const auto a    = r.at("address").get<unsigned>();
            if (a < ioStart || a >= ramStart)
                fail(path, "Register address is outside I/O memory");
            if (!addresses.insert(a).second)
                fail(path, "Duplicate register address");
        }
        for (const auto& [name, v] : d.at("vectors").items())
            if (!vectorNumbers.insert(v.get<unsigned>()).second)
                fail(child("/vectors", name), "Duplicate interrupt vector");
        const auto registerRef = [&](const Json& value, const std::string& path) {
            if (!d.at("registers").contains(value.get<std::string>()))
                fail(path, "Unknown register: " + value.get<std::string>());
        };
        std::set<std::string> cpuRegisters;
        for (const char* key : {"spmcsr", "smcr", "spl", "sph", "sreg"}) {
            if (!d.at("cpu").contains(key)) continue;
            registerRef(d.at("cpu").at(key), std::string("/cpu/") + key);
            if (!cpuRegisters.insert(d.at("cpu").at(key).get<std::string>()).second)
                fail(std::string("/cpu/") + key, "CPU registers must be distinct");
        }
        std::set<std::string> peripheralNames, gpios, pinNames, assignedRegisters;
        for (const auto& p : d.at("package").at("pins")) {
            if (!physicalNumbers.insert(p.at("n").get<unsigned>()).second)
                fail("/package/pins", "Duplicate physical pin number");
        }
        const auto pinCount = d.at("package").at("pins").size();
        if (pinCount % 2 != 0 || *physicalNumbers.rbegin() != pinCount)
            fail("/package/pins", "Package pins must be consecutively numbered from 1, with an even pin count");
        for (std::size_t i = 0; i < d.at("peripherals").size(); ++i) {
            const auto&       p    = d.at("peripherals")[i];
            const auto        path = "/peripherals/" + std::to_string(i);
            const std::string name = p.at("name");
            if (!peripheralNames.insert(name).second)
                fail(path + "/name", "Duplicate peripheral name");
            if (p.at("type") == "avr.gpio") {
                gpios.insert(name);
                for (const auto& n : p.at("pins"))
                    if (!pinNames.insert(n.get<std::string>()).second)
                        fail(path + "/pins", "Pin wired to multiple GPIO bits");
            }
        }
        std::set<std::string> physicalPorts, labels;
        for (const auto& p : d.at("package").at("pins")) {
            if (!p.at("port").is_null() && !physicalPorts.insert(p.at("port").get<std::string>()).second)
                fail("/package/pins", "Duplicate port pin");
            const std::string label = p.at("label");
            if (!label.empty() && !labels.insert(label).second)
                fail("/package/pins", "Duplicate board label");
        }
        for (const auto& n : pinNames)
            if (!physicalPorts.contains(n))
                fail("/package/pins", "Missing GPIO pin: " + n);
        const auto gpioRef = [&](const Json& v, const std::string& path) {
            if (!gpios.contains(v.get<std::string>()))
                fail(path, "Unknown GPIO port: " + v.get<std::string>());
        };
        for (const char* key : {"spl", "sph", "sreg"})
            assignedRegisters.insert(d.at("cpu").at(key).get<std::string>());
        std::set<std::string> assignedVectors;
        unsigned              adcCount = 0, eepromCount = 0;
        for (std::size_t i = 0; i < d.at("peripherals").size(); ++i) {
            const auto& p    = d.at("peripherals")[i];
            const auto  path = "/peripherals/" + std::to_string(i);
            for (const auto& [key, value] : p.at("registers").items()) {
                registerRef(value, path + "/registers/" + key);
                if (!assignedRegisters.insert(value.get<std::string>()).second)
                    fail(path + "/registers/" + key, "Register mapped by multiple peripherals");
            }
            const auto vectorRef = [&](const Json& v, const std::string& at) {
                if (!d.at("vectors").contains(v.get<std::string>()))
                    fail(at, "Unknown interrupt vector: " + v.get<std::string>());
                if (d.at("vectors").at(v.get<std::string>()) == 0)
                    fail(at, "Reset is not a peripheral interrupt");
                if (!assignedVectors.insert(v.get<std::string>()).second)
                    fail(at, "Interrupt vector assigned to multiple peripheral sources");
            };
            if (p.contains("vector"))
                vectorRef(p.at("vector"), path + "/vector");
            if (p.contains("vectors"))
                for (const auto& [key, value] : p.at("vectors").items())
                    vectorRef(value, path + "/vectors/" + key);
            if (p.contains("mcucr"))
                registerRef(p.at("mcucr"), path + "/mcucr");
            if (p.contains("port"))
                gpioRef(p.at("port"), path + "/port");
            if (p.contains("outputs"))
                for (std::size_t k = 0; k < p.at("outputs").size(); ++k)
                    gpioRef(p.at("outputs")[k].at("port"), path + "/outputs/" + std::to_string(k) + "/port");
            if (p.at("type") == "avr.adc") {
                if (++adcCount > 1)
                    fail(path + "/type", "Only one ADC instance is supported by this AVR model");
                for (const auto& n : p.at("channels"))
                    if (!pinNames.contains(n.get<std::string>()))
                        fail(path + "/channels", "ADC channel is not wired to GPIO");
            }
            if (p.at("type") == "avr.eeprom" && ++eepromCount > 1)
                fail(path + "/type", "Only one EEPROM controller is supported");
        }
        return {};
    } catch (const LoadError& e) {
        return std::unexpected(e);
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
}

nlohmann::json builtinDescription(std::string_view name) {
    if (name == "atmega328p")
        return Json::parse(embedded::atmega328p);
    throw std::invalid_argument("Unknown built-in MCU");
}
std::expected<std::unique_ptr<Mcu>, LoadError> loadJson(std::string_view text) {
    try {
        auto d = Json::parse(text);
        if (auto result = validate(d); !result)
            return std::unexpected(result.error());
        return std::make_unique<avr::Device>(std::move(d));
    } catch (const std::exception& e) {
        return std::unexpected(LoadError{"", e.what()});
    }
}
std::expected<std::unique_ptr<Mcu>, LoadError> load(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input)
        return std::unexpected(LoadError{"", "Cannot open MCU description: " + path.string()});
    return loadJson(std::string{std::istreambuf_iterator<char>{input}, {}});
}
}  // namespace siliconforge::mcu
