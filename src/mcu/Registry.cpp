#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

namespace siliconforge::mcu {

namespace {
struct Entry {
    std::string_view                     name;
    std::function<std::unique_ptr<Mcu>()> factory;
};

// Adding a microcontroller: implement siliconforge::Mcu under mcu/<chip>/ and register it here.
const std::vector<Entry>& entries() {
    static const std::vector<Entry> list{
        {"atmega328p", [] { return std::make_unique<Atmega328p>(); }},
    };
    return list;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}
}  // namespace

std::unique_ptr<Mcu> create(std::string_view name) {
    for (const auto& entry : entries())
        if (equalsIgnoringCase(entry.name, name))
            return entry.factory();
    return nullptr;
}

std::vector<std::string_view> available() {
    std::vector<std::string_view> names;
    for (const auto& entry : entries())
        names.push_back(entry.name);
    return names;
}

}  // namespace siliconforge::mcu
