#include <siliconforge/core/loader/ProgramImage.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>
#include <vector>

using namespace siliconforge;

namespace {
/// One Intel HEX record with its checksum.
std::string record(u8 type, u16 address, std::vector<u8> data) {
    auto line = std::format(":{:02X}{:04X}{:02X}", data.size(), address, type);
    u8   sum  = static_cast<u8>(data.size() + (address >> 8) + (address & 0xFF) + type);
    for (auto byte : data) {
        line += std::format("{:02X}", byte);
        sum = static_cast<u8>(sum + byte);
    }
    return line + std::format("{:02X}\n", static_cast<u8>(-sum));
}

const std::string endOfFile = ":00000001FF\n";

template <typename T>
void putLittleEndian(std::vector<u8>& bytes, std::size_t offset, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bytes[offset + i] = static_cast<u8>(value >> (8 * i));
}
}  // namespace

SCENARIO("Intel HEX files are parsed into segments", "[core][loader]") {
    WHEN("contiguous data records follow each other") {
        auto image = parseIntelHex(record(0, 0, {1, 2, 3, 4}) + record(0, 4, {5, 6}) + record(0, 0x100, {7}) + endOfFile);
        REQUIRE(image);
        THEN("they are merged into one segment, a gap starts another") {
            REQUIRE(image->segments.size() == 2);
            CHECK(image->segments[0].bytes == std::vector<u8>{1, 2, 3, 4, 5, 6});
            CHECK(image->segments[1].address == 0x100);
            CHECK(image->size() == 7);
        }
    }
    WHEN("an extended linear address record precedes data") {
        auto image = parseIntelHex(record(4, 0, {0x00, 0x01}) + record(0, 0x10, {0xAA}) + endOfFile);
        REQUIRE(image);
        CHECK(image->segments[0].address == 0x10010);
    }
    WHEN("Windows line endings are used") {
        CHECK(parseIntelHex(":0100000055AA\r\n:00000001FF\r\n"));
    }
    WHEN("a checksum is wrong") {
        auto image = parseIntelHex(":0100000055AB\n" + endOfFile);
        REQUIRE_FALSE(image);
        CHECK(image.error().line == 1);
        CHECK(image.error().message == "bad checksum");
    }
    WHEN("the end-of-file record is missing") {
        auto image = parseIntelHex(record(0, 0, {1}));
        REQUIRE_FALSE(image);
        CHECK(image.error().message == "missing end-of-file record");
    }
}

SCENARIO("ELF executables are loaded from their program headers", "[core][loader]") {
    // A minimal avr-gcc-like ELF: .text at 0, .data initialisers stored after it (LMA 4, VMA 0x800100).
    std::vector<u8> elf(52 + 2 * 32 + 6, 0);
    elf[0] = 0x7F, elf[1] = 'E', elf[2] = 'L', elf[3] = 'F', elf[4] = 1, elf[5] = 1;
    putLittleEndian<u32>(elf, 0x1C, 52);  // e_phoff
    putLittleEndian<u16>(elf, 0x2A, 32);  // e_phentsize
    putLittleEndian<u16>(elf, 0x2C, 2);   // e_phnum
    const auto programHeader = [&](std::size_t index, u32 offset, u32 virtualAddress, u32 physical, u32 size) {
        const auto at = 52 + index * 32;
        putLittleEndian<u32>(elf, at, 1);  // PT_LOAD
        putLittleEndian<u32>(elf, at + 4, offset);
        putLittleEndian<u32>(elf, at + 8, virtualAddress);
        putLittleEndian<u32>(elf, at + 12, physical);
        putLittleEndian<u32>(elf, at + 16, size);
        putLittleEndian<u32>(elf, at + 20, size);
    };
    programHeader(0, 116, 0, 0, 4);
    programHeader(1, 120, 0x800100, 4, 2);
    for (u8 i = 0; i < 6; ++i)
        elf[116 + i] = static_cast<u8>(0x10 + i);

    auto image = parseElf(elf);
    REQUIRE(image);
    THEN("both segments land in program memory at their load addresses") {
        REQUIRE(image->segments.size() == 1);
        CHECK(image->segments[0].bytes == std::vector<u8>{0x10, 0x11, 0x12, 0x13, 0x14, 0x15});
    }
    CHECK_FALSE(parseElf(std::vector<u8>(10, 0)));
}
