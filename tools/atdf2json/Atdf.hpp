#pragma once
// Imports the authoritative fields of a Microchip ATDF device file into a SiliconForge MCU description
// (docs/MCU-DESCRIPTIONS.md). The fields ATDF does not carry (timings, peripheral factories, clock, board labels) are
// left empty, or kept from a hand-authored base description.

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace siliconforge::atdf {

/// Keys keep their order, so that a re-import with a base keeps the layout of the shipped description.
using Json = nlohmann::ordered_json;

/// Parses an ATDF document; throws std::runtime_error with the parser's diagnostic and offset.
pugi::xml_document parse(std::string_view xml);

/// Converts the first device of `root` (the document or its root element) with the pinout `package` (the first one
/// when absent). `base`, when given, is copied and only the fields ATDF supplies are replaced in the copy.
/// Throws std::runtime_error on an ATDF the importer cannot represent, naming the missing element or attribute.
Json convert(const pugi::xml_node& root, const std::optional<std::string>& package = {}, const Json* base = nullptr);

/// The description as written by the tool: two-space indentation, UTF-8, a final newline.
std::string dump(const Json& description);

/// Python's int(text, 0): optional sign, 0x/0o/0b prefixes, underscores between digits, no leading zero in decimal.
long long number(std::string_view text);

}  // namespace siliconforge::atdf
