#pragma once

#include <nlohmann/json.hpp>
#include <siliconforge/core/DescriptionError.hpp>

#include <compare>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Components and circuits described in JSON (ADR 0005): `components/<type>/component.json` validated by
/// `schemas/component.schema.json`, `circuits/<name>.json` validated by `schemas/circuit.schema.json`.
namespace siliconforge::circuit {

using LoadError = DescriptionError;

/// One hole of a breadboard: "e6" on a terminal strip, "T+12" or "B-3" on a power rail.
struct Hole {
    char     row;       ///< 'a'..'j' on a terminal strip; 'T' (top) or 'B' (bottom) on a power rail.
    char     polarity;  ///< '+' or '-' on a power rail, 0 on a terminal strip.
    unsigned column;    ///< From 1.

    [[nodiscard]] bool isRail() const noexcept {
        return polarity != 0;
    }
    [[nodiscard]] auto operator<=>(const Hole&) const = default;
};

/// Parses the hole syntax of the circuit schema. Returns nullopt for anything else.
[[nodiscard]] std::optional<Hole> parseHole(std::string_view text);

/// The component types a circuit may use, by type name.
class Library {
public:
    /// The types embedded in the binary: every `components/<type>/component.json` of the source tree.
    [[nodiscard]] static const Library& builtin();

    /// Validates a component description and adds it. A type already present is an error.
    std::expected<void, LoadError> add(nlohmann::json component);
    std::expected<void, LoadError> addFile(const std::filesystem::path& path);

    /// Description of a type, nullptr when it is unknown.
    [[nodiscard]] const nlohmann::json*    find(std::string_view type) const noexcept;
    [[nodiscard]] std::vector<std::string> types() const;

private:
    std::map<std::string, nlohmann::json, std::less<>> components;
};

/// Schema, then consistency: terminals, property defaults and bounds, footprint, model elements and their state.
[[nodiscard]] std::expected<void, LoadError> validateComponent(const nlohmann::json& component);

/// Schema, then consistency with the library: known types, terminals, footprints, holes and property values.
[[nodiscard]] std::expected<void, LoadError> validateCircuit(const nlohmann::json& circuit, const Library& library = Library::builtin());

/// Parses and validates a circuit description.
[[nodiscard]] std::expected<nlohmann::json, LoadError> parseDescription(std::string_view text, const Library& library = Library::builtin());
[[nodiscard]] std::expected<nlohmann::json, LoadError> loadDescription(const std::filesystem::path& path, const Library& library = Library::builtin());

/// A circuit embedded in the binary ("arduino-demo"). Throws std::invalid_argument for an unknown name.
[[nodiscard]] nlohmann::json builtinDescription(std::string_view name);

}  // namespace siliconforge::circuit
