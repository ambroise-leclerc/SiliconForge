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

/// Straight distance between two holes in hole pitches: columns, and rows as they lie on a real board (the rails one
/// pitch apart and two pitches from the strips, a central channel two pitches wide). Limits the length of a wire.
[[nodiscard]] double distance(const Hole& a, const Hole& b);

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

    /// The rendering module of a type (`components/<type>/render.js`), empty when the type has none. Built-in types
    /// carry theirs; setRenderer gives one to a type added with add().
    [[nodiscard]] std::string_view renderer(std::string_view type) const noexcept;
    void                           setRenderer(std::string_view type, std::string script);

private:
    std::map<std::string, nlohmann::json, std::less<>> components;
    std::map<std::string, std::string, std::less<>>    renderers;
};

/// Schema, then consistency: terminals, property defaults and bounds, footprint, model elements and their state.
[[nodiscard]] std::expected<void, LoadError> validateComponent(const nlohmann::json& component);

/// Schema, then consistency with the library: known types, terminals, footprints, holes and property values.
[[nodiscard]] std::expected<void, LoadError> validateCircuit(const nlohmann::json& circuit, const Library& library = Library::builtin());

/// Parses and validates a circuit description.
[[nodiscard]] std::expected<nlohmann::json, LoadError> parseDescription(std::string_view text, const Library& library = Library::builtin());
[[nodiscard]] std::expected<nlohmann::json, LoadError> loadDescription(const std::filesystem::path& path, const Library& library = Library::builtin());

/// Why move() refused to place a component.
struct MoveError {
    enum class Kind { UnknownComponent, Invalid } kind;
    LoadError error;  ///< For Invalid: the JSON Pointer and the rule broken, as validateCircuit reports them.
};

/// The circuit with some terminals of one component moved to other holes, validated. `id` is a component id, or
/// "wire-<n>" for the n-th wire (from 1); `holes` maps terminal names ("a", "anode", "from") to holes ("e6", "T+3"),
/// and the terminals it does not name stay where they are.
[[nodiscard]] std::expected<nlohmann::json, MoveError>
move(nlohmann::json circuit, std::string_view id, const nlohmann::json& holes, const Library& library = Library::builtin());

/// A circuit embedded in the binary ("arduino-demo"): every `circuits/<name>.json` of the source tree. Throws
/// std::invalid_argument for an unknown name.
[[nodiscard]] nlohmann::json builtinDescription(std::string_view name);
/// Names of the embedded circuits, sorted.
[[nodiscard]] std::vector<std::string> builtinNames();

}  // namespace siliconforge::circuit
