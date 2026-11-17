#include "Atdf.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace siliconforge::atdf {
namespace {

using Node = pugi::xml_node;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

/// The element name without its namespace prefix: vendor ATDF wrappers may qualify them.
std::string_view localName(const Node& node) {
    const std::string_view name = node.name();
    const auto colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

std::vector<Node> children(const Node& node, std::string_view name) {
    std::vector<Node> result;
    for (const Node& c : node.children())
        if (c.type() == pugi::node_element && localName(c) == name)
            result.push_back(c);
    return result;
}

/// ElementTree's findall on a plain path such as "devices/device".
std::vector<Node> findAll(const Node& node, std::string_view path) {
    std::vector<Node> nodes{node};
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto name = path.substr(0, slash);
        std::vector<Node> next;
        for (const Node& n : nodes)
            for (const Node& c : children(n, name))
                next.push_back(c);
        nodes = std::move(next);
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
    }
    return nodes;
}

std::optional<std::string> get(const Node& node, const char* attribute) {
    const auto a = node.attribute(attribute);
    return a ? std::optional<std::string>{a.value()} : std::nullopt;
}

std::string get(const Node& node, const char* attribute, const std::string& fallback) {
    return get(node, attribute).value_or(fallback);
}

/// Present and not empty, as Python's `if element.get(attribute):`.
bool given(const Node& node, const char* attribute) { return !get(node, attribute, "").empty(); }

std::string required(const Node& node, const char* attribute) {
    auto value = get(node, attribute);
    if (!value)
        fail("<" + std::string(localName(node)) + "> lacks required attribute '" + attribute + "'");
    return *value;
}

/// Python's str(None) in the messages that name an absent attribute.
std::string shown(const std::optional<std::string>& value) { return value.value_or("None"); }

int byteOf(long long value, int byte) { return static_cast<int>((static_cast<unsigned long long>(value) >> (8 * byte)) & 255U); }

bool isPortPad(const std::string& pad) {
    return pad.size() >= 3 && pad[0] == 'P' && std::isupper(static_cast<unsigned char>(pad[1])) &&
           std::all_of(pad.begin() + 2, pad.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::string signalFunction(const Node& module, const Node& instance, const std::string& group, const std::string& index) {
    const std::string moduleName = get(module, "name", "");
    if (moduleName == "CPU" && group == "CLK" && index == "0")
        return "CLKO";  // ATDF models the clock-output signal as CLK with index 0.
    if (moduleName.starts_with("TC")) {
        std::string timer;
        for (char c : get(instance, "name", ""))
            if (std::isdigit(static_cast<unsigned char>(c)))
                timer += c;
        if (group.size() == 3 && group.starts_with("OC") && std::isupper(static_cast<unsigned char>(group[2])))
            return "OC" + timer + group[2];
        return group == "T" || group == "ICP" ? group + timer : group;
    }
    return group + index;
}

}  // namespace

long long number(std::string_view text) {
    const auto invalid = [&] { fail("invalid literal for int() with base 0: '" + std::string(text) + "'"); };
    std::string_view s = text;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    bool negative = false;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    int base = 10;
    if (s.size() > 1 && s[0] == '0' && std::isalpha(static_cast<unsigned char>(s[1]))) {
        const char p = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
        base = p == 'x' ? 16 : p == 'o' ? 8 : p == 'b' ? 2 : 0;
        if (!base)
            invalid();
        s.remove_prefix(2);
        if (!s.empty() && s.front() == '_')
            s.remove_prefix(1);  // 0x_1F is valid.
    } else if (s.size() > 1 && s[0] == '0' && s.find_first_not_of("0_") != std::string_view::npos) {
        invalid();  // Decimal literals do not start with 0.
    }
    if (s.empty() || s.front() == '_' || s.back() == '_' || s.find("__") != std::string_view::npos)
        invalid();
    unsigned long long value = 0;
    for (char c : s) {
        if (c == '_')
            continue;
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                          : std::isalpha(static_cast<unsigned char>(c))
                              ? std::tolower(static_cast<unsigned char>(c)) - 'a' + 10
                              : 99;
        if (digit >= base)
            invalid();
        if (value > (~0ULL - static_cast<unsigned>(digit)) / static_cast<unsigned>(base))
            fail("integer out of range: '" + std::string(text) + "'");
        value = value * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
    }
    if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
        fail("integer out of range: '" + std::string(text) + "'");
    return negative ? -static_cast<long long>(value) : static_cast<long long>(value);
}

pugi::xml_document parse(std::string_view xml) {
    pugi::xml_document document;
    const auto result = document.load_buffer(xml.data(), xml.size());
    if (!result)
        fail(std::string(result.description()) + " at offset " + std::to_string(result.offset));
    return document;
}

Json convert(const Node& start, const std::optional<std::string>& package, const Json* base) {
    const Node root = start.type() == pugi::node_document
                          ? start.find_child([](const Node& n) { return n.type() == pugi::node_element; })
                          : start;
    const auto devices = findAll(root, "devices/device");
    if (devices.empty())
        fail("ATDF contains no device");
    const Node device = devices.front();
    const auto architecture = get(device, "architecture");
    if (architecture != "AVR8" && architecture != "AVR8X")
        fail("Only AVR ATDF descriptions are supported");
    Json result = base ? *base
                       : Json{{"formatVersion", 1}, {"architecture", "avr"}, {"core", nullptr}, {"clockHz", nullptr},
                              {"supplyVoltage", nullptr}, {"cpu", Json::object()}, {"timing", Json::object()},
                              {"peripherals", Json::array()}, {"memories", Json::object()}, {"registers", Json::object()}};
    if (const auto name = get(device, "name"))
        result["name"] = *name;
    else
        result["name"] = nullptr;

    const auto segments = findAll(device, "address-spaces/address-space/memory-segment");
    for (const auto& [key, name] : {std::pair{"flash", "FLASH"}, {"sram", "IRAM"}, {"io", "MAPPED_IO"},
                                    {"registers", "REGISTERS"}, {"eeprom", "EEPROM"}}) {
        const auto segment = std::ranges::find_if(segments, [&](const Node& s) { return get(s, "name") == name; });
        if (segment == segments.end())
            fail(std::string("ATDF has no ") + name + " memory segment");
        Json value{{"start", number(required(*segment, "start"))}, {"size", number(required(*segment, "size"))}};
        if (given(*segment, "pagesize"))
            value["pageSize"] = number(get(*segment, "pagesize", ""));
        result["memories"][key] = value;
    }

    // Follow device instance references: offsets are relative to the referenced register group.
    const Json oldRegisters = result.contains("registers") ? result["registers"] : Json::object();
    std::map<std::string, Json> registers;  // Sorted by name, as the shipped descriptions are.
    const auto definitions = findAll(root, "modules/module");
    for (const Node& module : findAll(device, "peripherals/module")) {
        const auto definition = std::ranges::find_if(definitions, [&](const Node& m) { return get(m, "name") == get(module, "name"); });
        if (definition == definitions.end())
            continue;
        for (const Node& instance : children(module, "instance")) {
            for (const Node& reference : children(instance, "register-group")) {
                if (get(reference, "address-space") != "data")
                    continue;
                const auto groupName = get(reference, "name-in-module");
                const auto groups = children(*definition, "register-group");
                const auto group = std::ranges::find_if(groups, [&](const Node& g) { return get(g, "name") == shown(groupName); });
                if (group == groups.end())
                    fail("Unresolved register group " + shown(groupName));
                const long long offset = number(get(reference, "offset", "0"));
                for (const Node& registerNode : children(*group, "register")) {
                    const long long size = number(get(registerNode, "size", "1"));
                    const std::string name = required(registerNode, "name");
                    for (int byte = 0; byte < size; ++byte) {
                        const std::string byteName =
                            name + (size == 1 ? "" : byte == 0 ? "L" : byte == 1 ? "H" : std::to_string(byte));
                        const Json old = oldRegisters.contains(byteName) ? oldRegisters[byteName] : Json::object();
                        Json value{{"address", offset + number(required(registerNode, "offset")) + byte},
                                   {"reset", old.contains("reset") ? old["reset"] : Json(0)},
                                   {"bits", Json::object()}};
                        if (given(registerNode, "initval"))
                            value["reset"] = byteOf(number(get(registerNode, "initval", "")), byte);
                        for (const Node& bit : children(registerNode, "bitfield")) {
                            const int mask = byteOf(number(required(bit, "mask")), byte);
                            if (mask)
                                value["bits"][required(bit, "name")] = mask;
                        }
                        if (const auto previous = registers.find(byteName); previous != registers.end()) {
                            if (previous->second["address"] != value["address"])
                                fail("Register " + byteName + " is defined at several addresses");
                            // ATDF may split one register's bits across modules (e.g. GTCCR, TIMSK).
                            Json bits = previous->second["bits"];
                            for (const auto& [bitName, mask] : value["bits"].items())
                                bits[bitName] = mask;
                            value["bits"] = bits;
                            if (!given(registerNode, "initval"))
                                value["reset"] = previous->second["reset"];
                        }
                        registers[byteName] = value;
                    }
                }
            }
        }
    }
    result["registers"] = Json::object();
    for (auto& [name, value] : registers)
        result["registers"][name] = std::move(value);

    Json vectors = Json::object();
    for (const Node& vector : findAll(device, "interrupts/interrupt")) {
        std::string name = required(vector, "name");
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        vectors[name] = number(required(vector, "index"));
    }
    result["vectors"] = vectors;

    const auto pinouts = findAll(root, "pinouts/pinout");
    if (pinouts.empty())
        fail("ATDF contains no pinout");
    auto selected = package ? std::ranges::find_if(pinouts, [&](const Node& p) { return get(p, "name") == *package; })
                            : pinouts.begin();
    if (selected == pinouts.end()) {
        std::string names;
        for (const Node& p : pinouts)
            names += (names.empty() ? "" : ", ") + shown(get(p, "name"));
        fail("Unknown package " + *package + "; available: " + names);
    }
    const Json oldPackage = result.contains("package") ? result["package"] : Json::object();
    std::map<long long, Json> oldPins;
    if (oldPackage.contains("pins"))
        for (const Json& pin : oldPackage["pins"])
            oldPins[pin.at("n").get<long long>()] = pin;

    std::map<std::string, std::set<std::string>> functions;
    for (const Node& module : findAll(device, "peripherals/module"))
        for (const Node& instance : children(module, "instance"))
            for (const Node& signal : findAll(instance, "signals/signal")) {
                const std::string group = required(signal, "group");
                if (group == "P")
                    continue;
                const std::string function = signalFunction(module, instance, group, get(signal, "index", ""));
                functions[required(signal, "pad")].insert(function);
            }

    std::vector<Json> pins;
    for (const Node& pin : children(*selected, "pin")) {
        const long long n = number(required(pin, "position"));
        const std::string pad = required(pin, "pad");
        const Json port = isPortPad(pad) ? Json(pad) : Json(nullptr);
        const Json old = oldPins.contains(n) ? oldPins[n] : Json::object();
        // Board labels, display colours and layout slots are not supplied by ATDF.
        const Json oldPort = old.contains("port") ? old["port"] : Json(nullptr);
        bool samePad = oldPort == port;
        if (samePad && port.is_null()) {
            const std::string first = old.contains("labels") ? old["labels"].at(0).get<std::string>() : std::string{};
            samePad = first.substr(0, first.find(':')) == pad;
        }
        Json entry{{"n", n}, {"port", port},
                   {"label", samePad && old.contains("label") ? old["label"] : Json("")},
                   {"labels", samePad && old.contains("labels") ? old["labels"] : Json::array({pad})},
                   {"functions", Json::array()}};
        if (const auto f = functions.find(pad); f != functions.end())
            for (const auto& function : f->second)
                entry["functions"].push_back(function);
        pins.push_back(std::move(entry));
    }
    std::ranges::stable_sort(pins, {}, [](const Json& p) { return p["n"].get<long long>(); });
    Json packageName = get(*selected, "name") ? Json(*get(*selected, "name")) : Json(nullptr);
    result["package"] = Json{{"name", packageName}, {"pins", pins},
                             {"slotKinds", oldPackage.contains("slotKinds")
                                               ? oldPackage["slotKinds"]
                                               : Json::array({"port", "digital", "function", "ide", "pwm", "function"})}};
    return result;
}

std::string dump(const Json& description) { return description.dump(2) + "\n"; }

}  // namespace siliconforge::atdf
