#pragma once
// Shared by the description loaders (MCU, components, circuits): schema errors located by a JSON Pointer.

#include <nlohmann/json-schema.hpp>
#include <siliconforge/core/DescriptionError.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace siliconforge::detail {

using Json = nlohmann::json;

/// JSON Pointer of `key` under `path`, with the key escaped.
inline std::string child(const std::string& path, std::string_view key) {
    return (Json::json_pointer{path} / std::string(key)).to_string();
}

/// Keeps the first schema error. A missing property is reported at the path it should have had.
struct SchemaErrors : nlohmann::json_schema::error_handler {
    std::optional<DescriptionError> first;
    void                            error(const Json::json_pointer& pointer, const Json&, const std::string& message) override {
        if (first)
            return;
        auto              path   = pointer.to_string();
        const std::string prefix = "required property '";
        if (message.starts_with(prefix))
            path = child(path, message.substr(prefix.size(), message.find("'", prefix.size()) - prefix.size()));
        first = DescriptionError{path, message};
    }
};

[[noreturn]] inline void fail(std::string path, std::string message) {
    throw DescriptionError{std::move(path), std::move(message)};
}

}  // namespace siliconforge::detail
