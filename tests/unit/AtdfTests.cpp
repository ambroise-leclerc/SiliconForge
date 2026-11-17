// The ATDF importer of tools/atdf2json, on a synthetic device: offset register groups, a 16-bit register, a vector and
// a signal, two packages. Each case edits the document the way the ATDF variants it covers do.
#include <Atdf.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using siliconforge::atdf::convert;
using siliconforge::atdf::Json;
using siliconforge::atdf::number;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::string atdf = R"(<avr-tools-device-file><devices><device name="TestAVR" architecture="AVR8">
<address-spaces><address-space>
<memory-segment name="FLASH" start="0" size="0x4000" pagesize="0x80"/>
<memory-segment name="IRAM" start="0x100" size="0x400"/>
<memory-segment name="MAPPED_IO" start="0x20" size="0xe0"/>
<memory-segment name="REGISTERS" start="0" size="32"/>
<memory-segment name="EEPROM" start="0" size="512"/>
</address-space></address-spaces><peripherals><module name="USART"><instance name="USART0">
<register-group name-in-module="USART" offset="0x20" address-space="data"/>
<signals><signal group="TXD" pad="PD1"/></signals></instance></module></peripherals>
<interrupts><interrupt name="RESET" index="0"/><interrupt name="USART_TX" index="20"/></interrupts>
</device></devices><modules><module name="USART"><register-group name="USART">
<register name="BAUD" offset="4" size="2" initval="0x1234"><bitfield name="VALUE" mask="0xfff"/></register>
</register-group></module></modules><pinouts><pinout name="DIP2"><pin position="1" pad="PD1"/>
<pin position="2" pad="GND"/></pinout><pinout name="OTHER"><pin position="1" pad="PD0"/>
<pin position="2" pad="GND"/></pinout></pinouts></avr-tools-device-file>)";

pugi::xml_document document(const std::string& text = atdf) { return siliconforge::atdf::parse(text); }

pugi::xml_node path(const pugi::xml_document& d, const char* xpath) { return d.select_node(xpath).node(); }

pugi::xml_node append(pugi::xml_node parent, const char* name, std::initializer_list<std::pair<const char*, const char*>> attributes) {
    auto child = parent.append_child(name);
    for (const auto& [key, value] : attributes)
        child.append_attribute(key) = value;
    return child;
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    text.replace(text.find(from), from.size(), to);
    return text;
}

std::string conversionError(const pugi::xml_document& d, const std::string& package = "DIP2") {
    try {
        convert(d, package);
    } catch (const std::exception& error) {
        return error.what();
    }
    return "no error";
}

const Json& firstPin(const Json& d) { return d["package"]["pins"][0]; }

}  // namespace

TEST_CASE("ATDF imports offsets, split registers, vectors, memories and functions", "[atdf]") {
    const Json d = convert(document(), std::string("DIP2"));
    CHECK(d["name"] == "TestAVR");
    CHECK(d["memories"]["flash"] == Json{{"start", 0}, {"size", 16384}, {"pageSize", 128}});
    CHECK(d["registers"]["BAUDL"] == Json{{"address", 36}, {"reset", 52}, {"bits", {{"VALUE", 255}}}});
    CHECK(d["registers"]["BAUDH"] == Json{{"address", 37}, {"reset", 18}, {"bits", {{"VALUE", 15}}}});
    CHECK(d["vectors"]["USART_TX"] == 20);
    CHECK(firstPin(d)["functions"] == Json::array({"TXD"}));
    CHECK(d["clockHz"].is_null());  // No fabricated timing defaults.
}

TEST_CASE("ATDF names the CPU clock output CLKO, not CLK0", "[atdf]") {
    auto d = document();
    auto signals = append(append(append(path(d, "/avr-tools-device-file/devices/device/peripherals"), "module", {{"name", "CPU"}}),
                                 "instance", {{"name", "CPU"}}),
                          "signals", {});
    append(signals, "signal", {{"group", "CLK"}, {"index", "0"}, {"pad", "PD1"}});
    append(signals, "signal", {{"group", "CLK"}, {"index", "1"}, {"pad", "PD0"}});
    CHECK(firstPin(convert(d, std::string("DIP2")))["functions"] == Json::array({"CLKO", "TXD"}));
    CHECK(firstPin(convert(d, std::string("OTHER")))["functions"] == Json::array({"CLK1"}));
}

TEST_CASE("ATDF with a base keeps the fields ATDF does not carry", "[atdf]") {
    Json base = convert(document(), std::string("DIP2"));
    base["timing"] = {{"nop", {{"cycles", 9}}}};
    base["package"]["pins"][0]["label"] = "D1";
    base["memories"]["flash"]["size"] = 2;
    const Json original = base;
    const Json d = convert(document(), std::string("DIP2"), &base);
    CHECK(base == original);
    CHECK(d["timing"] == base["timing"]);
    CHECK(firstPin(d)["label"] == "D1");
    CHECK(d["memories"]["flash"]["size"] == 16384);
    CHECK(firstPin(convert(document(), std::string("OTHER"), &base))["label"] == "");
}

TEST_CASE("ATDF reports the missing required attributes", "[atdf]") {
    CHECK_THAT(conversionError(document(replaced(atdf, R"(<register name="BAUD" offset="4")", R"(<register name="BAUD")"))),
               ContainsSubstring("'offset'"));
    CHECK_THAT(conversionError(document(replaced(atdf, R"(<pin position="1" pad="PD1"/>)", R"(<pin position="1"/>)"))),
               ContainsSubstring("'pad'"));
}

TEST_CASE("ATDF merges the bits of a register split across modules", "[atdf]") {
    auto d = document();
    auto module = append(path(d, "/avr-tools-device-file/devices/device/peripherals"), "module", {{"name", "EXTRA"}});
    append(append(module, "instance", {{"name", "EXTRA"}}), "register-group",
           {{"name-in-module", "EXTRA"}, {"offset", "0x20"}, {"address-space", "data"}});
    auto group = append(append(path(d, "/avr-tools-device-file/modules"), "module", {{"name", "EXTRA"}}), "register-group",
                        {{"name", "EXTRA"}});
    append(append(group, "register", {{"name", "BAUD"}, {"offset", "4"}, {"size", "2"}}), "bitfield",
           {{"name", "OTHER"}, {"mask", "0x1000"}});
    const Json result = convert(d, std::string("DIP2"));
    CHECK(result["registers"]["BAUDH"]["bits"] == Json{{"VALUE", 15}, {"OTHER", 16}});
    CHECK(result["registers"]["BAUDH"]["reset"] == 18);
}

TEST_CASE("ATDF numbers every timer compare output", "[atdf]") {
    auto d = document();
    auto module = append(path(d, "/avr-tools-device-file/devices/device/peripherals"), "module", {{"name", "TC16"}});
    append(append(append(module, "instance", {{"name", "TC3"}}), "signals", {}), "signal", {{"group", "OCC"}, {"pad", "PD1"}});
    CHECK(firstPin(convert(d, std::string("DIP2")))["functions"] == Json::array({"OC3C", "TXD"}));
}

TEST_CASE("ATDF rejects conflicting register definitions", "[atdf]") {
    auto d = document();
    append(path(d, "/avr-tools-device-file/devices/device/peripherals/module/instance"), "register-group",
           {{"name-in-module", "USART"}, {"offset", "0x40"}, {"address-space", "data"}});
    CHECK_THAT(conversionError(d), ContainsSubstring("several addresses"));
}

TEST_CASE("ATDF names the available packages when one is unknown", "[atdf]") {
    CHECK(conversionError(document(), "missing") == "Unknown package missing; available: DIP2, OTHER");
}

TEST_CASE("ATDF reads integers as Python's int(text, 0) does", "[atdf]") {
    CHECK(number("0x1F") == 31);
    CHECK(number("0X1f") == 31);
    CHECK(number(" 42 ") == 42);
    CHECK(number("0b101") == 5);
    CHECK(number("0o17") == 15);
    CHECK(number("-0x10") == -16);
    CHECK(number("1_000") == 1000);
    CHECK(number("0") == 0);
    CHECK(number("000") == 0);
    for (const char* invalid : {"010", "", "0x", "1__0", "_1", "1_", "0xG", "12a"})
        CHECK_THROWS_WITH(number(invalid), ContainsSubstring("invalid literal"));
}
