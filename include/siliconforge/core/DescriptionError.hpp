#pragma once

#include <string>

namespace siliconforge {

/// Error found while loading a JSON description (MCU, component or circuit).
struct DescriptionError {
    std::string path;  ///< JSON Pointer, empty for a document or file error.
    std::string message;
};

}  // namespace siliconforge
