#include <siliconforge/mcu/atmega328p/Atmega328p.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>

namespace siliconforge::mcu {

namespace {
struct Entry {
    std::string_view                      name;
    std::function<std::unique_ptr<Mcu>()> factory;
};

// Integrated descriptions and the additional descriptions discovered at startup.
std::vector<Entry>& entries() {
    static std::vector<Entry> list{
        {"atmega328p",
         [] {
             return std::make_unique<Atmega328p>();
         }},
    };
    return list;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}
}  // namespace

std::vector<std::pair<std::filesystem::path, LoadError>> addPath(const std::filesystem::path& directory) {
    // Catalog discovery is a startup operation; factories own the validated descriptions, so later edits cannot change a catalog entry.
    static std::map<std::string, std::string>                descriptions;
    std::vector<std::pair<std::filesystem::path, LoadError>> errors;
    std::error_code                                          ec;
    std::filesystem::directory_iterator                      files{directory, ec};
    if (ec) {
        errors.emplace_back(directory, LoadError{"", ec.message()});
        return errors;
    }
    std::vector<std::filesystem::path> paths;
    for (const auto& f : files)
        if (f.is_regular_file() && f.path().extension() == ".json")
            paths.push_back(f.path());
    std::ranges::sort(paths);
    for (const auto& path : paths) {
        std::ifstream stream{path};
        if (!stream) {
            errors.emplace_back(path, LoadError{"", "Cannot open MCU description"});
            continue;
        }
        std::string text{std::istreambuf_iterator<char>{stream}, {}};
        auto        device = loadJson(text);
        if (!device) {
            errors.emplace_back(path, device.error());
            continue;
        }
        std::string name{(*device)->name()};
        std::ranges::transform(name, name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const bool duplicate = std::ranges::any_of(entries(), [&](const Entry& e) {
            return equalsIgnoringCase(e.name, name);
        });
        if (duplicate) {
            errors.emplace_back(path, LoadError{"/name", "Duplicate MCU name: " + name});
            continue;
        }
        auto [it, inserted] = descriptions.emplace(name, std::move(text));
        entries().push_back({it->first, [text = it->second]() -> std::unique_ptr<Mcu> {
                                 auto result = loadJson(text);
                                 return result ? std::move(*result) : nullptr;
                             }});
    }
    return errors;
}

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
