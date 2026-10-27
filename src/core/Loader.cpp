#include <siliconforge/core/loader/ProgramImage.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>

namespace siliconforge {

namespace {

std::expected<u8, std::string> hexByte(std::string_view text, std::size_t position) {
    if (position + 2 > text.size())
        return std::unexpected("truncated record");
    unsigned value  = 0;
    const auto* begin = text.data() + position;
    const auto [end, error] = std::from_chars(begin, begin + 2, value, 16);
    if (error != std::errc{} || end != begin + 2)
        return std::unexpected(std::format("invalid hexadecimal digits '{}'", text.substr(position, 2)));
    return static_cast<u8>(value);
}

/// Appends bytes at an absolute address, merging with the previous segment when contiguous.
void place(ProgramImage& image, Address address, std::span<const u8> bytes, MemorySpace space = MemorySpace::Program) {
    if (bytes.empty())
        return;
    if (!image.segments.empty()) {
        auto& last = image.segments.back();
        if (last.space == space && last.address + last.bytes.size() == address) {
            last.bytes.insert(last.bytes.end(), bytes.begin(), bytes.end());
            return;
        }
    }
    image.segments.push_back(Segment{space, address, {bytes.begin(), bytes.end()}});
}

template <typename T>
T readLittleEndian(std::span<const u8> bytes, std::size_t offset) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<T>(static_cast<T>(bytes[offset + i]) << (8 * i));
    return value;
}

}  // namespace

LoadResult<ProgramImage> parseIntelHex(std::string_view text) {
    ProgramImage image;
    Address      base       = 0;
    std::size_t  lineNumber = 0;
    bool         ended      = false;

    while (!text.empty() && !ended) {
        const auto eol  = text.find('\n');
        auto       line = text.substr(0, eol);
        text            = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        ++lineNumber;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        if (line.empty())
            continue;
        if (line.front() != ':')
            return std::unexpected(LoadError{"record does not start with ':'", lineNumber});

        std::vector<u8> record;
        for (std::size_t position = 1; position < line.size(); position += 2) {
            auto byte = hexByte(line, position);
            if (!byte)
                return std::unexpected(LoadError{byte.error(), lineNumber});
            record.push_back(*byte);
        }
        if (record.size() < 5 || record.size() != std::size_t{record[0]} + 5u)
            return std::unexpected(LoadError{"record length does not match its byte count", lineNumber});
        u8 checksum = 0;
        for (auto byte : record)
            checksum = static_cast<u8>(checksum + byte);
        if (checksum != 0)
            return std::unexpected(LoadError{"bad checksum", lineNumber});

        const auto count   = record[0];
        const auto offset  = static_cast<Address>(record[1] << 8 | record[2]);
        const auto type    = record[3];
        const auto payload = std::span<const u8>{record}.subspan(4, count);
        switch (type) {
            case 0x00: place(image, base + offset, payload); break;
            case 0x01: ended = true; break;
            case 0x02:  // Extended segment address: base = value * 16
                if (count != 2)
                    return std::unexpected(LoadError{"bad extended segment address record", lineNumber});
                base = static_cast<Address>((payload[0] << 8 | payload[1]) << 4);
                break;
            case 0x04:  // Extended linear address: upper 16 bits
                if (count != 2)
                    return std::unexpected(LoadError{"bad extended linear address record", lineNumber});
                base = static_cast<Address>((payload[0] << 8 | payload[1]) << 16);
                break;
            case 0x03:
            case 0x05: break;  // Start addresses are irrelevant for a reset-vector based MCU.
            default: return std::unexpected(LoadError{std::format("unknown record type {:#04x}", type), lineNumber});
        }
    }
    if (!ended)
        return std::unexpected(LoadError{"missing end-of-file record", lineNumber});
    return image;
}

LoadResult<ProgramImage> parseElf(std::span<const u8> bytes, Address dataSpaceOffset, Address eepromOffset) {
    constexpr std::size_t headerSize = 52;
    if (bytes.size() < headerSize || bytes[0] != 0x7F || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F')
        return std::unexpected(LoadError{"not an ELF file"});
    if (bytes[4] != 1 || bytes[5] != 1)
        return std::unexpected(LoadError{"only 32-bit little-endian ELF files are supported"});

    const auto phOffset  = readLittleEndian<u32>(bytes, 0x1C);
    const auto shOffset  = readLittleEndian<u32>(bytes, 0x20);
    const auto phSize    = readLittleEndian<u16>(bytes, 0x2A);
    const auto phCount   = readLittleEndian<u16>(bytes, 0x2C);
    const auto shSize    = readLittleEndian<u16>(bytes, 0x2E);
    const auto shCount   = readLittleEndian<u16>(bytes, 0x30);
    const auto inBounds  = [&](std::size_t offset, std::size_t size) { return offset <= bytes.size() && size <= bytes.size() - offset; };

    ProgramImage image;
    for (u16 i = 0; i < phCount; ++i) {
        const std::size_t header = phOffset + std::size_t{i} * phSize;
        if (!inBounds(header, 32))
            return std::unexpected(LoadError{"program header out of file bounds"});
        constexpr u32 loadable = 1;
        if (readLittleEndian<u32>(bytes, header) != loadable)
            continue;
        const auto offset   = readLittleEndian<u32>(bytes, header + 4);
        const auto physical = readLittleEndian<u32>(bytes, header + 12);
        const auto fileSize = readLittleEndian<u32>(bytes, header + 16);
        if (!inBounds(offset, fileSize))
            return std::unexpected(LoadError{"segment out of file bounds"});
        const auto content = bytes.subspan(offset, fileSize);
        if (physical >= eepromOffset)
            place(image, physical - eepromOffset, content, MemorySpace::Eeprom);
        else if (physical >= dataSpaceOffset)
            place(image, physical - dataSpaceOffset, content, MemorySpace::Data);
        else
            place(image, physical, content);
    }
    std::ranges::sort(image.segments, {}, [](const Segment& segment) { return std::pair{segment.space, segment.address}; });

    // Function symbols, for disassembly and call stacks. Missing or stripped tables are not an error.
    for (u16 i = 0; i < shCount; ++i) {
        const std::size_t section = shOffset + std::size_t{i} * shSize;
        if (!inBounds(section, 40))
            break;
        constexpr u32 symbolTable = 2;
        if (readLittleEndian<u32>(bytes, section + 4) != symbolTable)
            continue;
        const auto symOffset = readLittleEndian<u32>(bytes, section + 16);
        const auto symSize   = readLittleEndian<u32>(bytes, section + 20);
        const auto link      = readLittleEndian<u32>(bytes, section + 24);
        const std::size_t strSection = shOffset + std::size_t{link} * shSize;
        if (link >= shCount || !inBounds(strSection, 40) || !inBounds(symOffset, symSize))
            break;
        const auto strOffset = readLittleEndian<u32>(bytes, strSection + 16);
        const auto strSize   = readLittleEndian<u32>(bytes, strSection + 20);
        if (!inBounds(strOffset, strSize))
            break;
        for (std::size_t entry = symOffset; entry + 16 <= symOffset + symSize; entry += 16) {
            constexpr u8 function = 2;
            if ((bytes[entry + 12] & 0x0F) != function)
                continue;
            const auto nameOffset = readLittleEndian<u32>(bytes, entry);
            const auto value      = readLittleEndian<u32>(bytes, entry + 4);
            if (nameOffset >= strSize || value >= dataSpaceOffset)
                continue;
            const auto* name = reinterpret_cast<const char*>(bytes.data() + strOffset + nameOffset);
            image.symbols.emplace(value, std::string{name, strnlen(name, strSize - nameOffset)});
        }
    }
    return image;
}

LoadResult<ProgramImage> loadProgram(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file)
        return std::unexpected(LoadError{std::format("cannot open '{}'", path.string())});
    const std::vector<u8> content{std::istreambuf_iterator<char>{file}, {}};
    if (content.size() >= 4 && content[0] == 0x7F && content[1] == 'E' && content[2] == 'L' && content[3] == 'F')
        return parseElf(content);
    return parseIntelHex(std::string_view{reinterpret_cast<const char*>(content.data()), content.size()});
}

}  // namespace siliconforge
