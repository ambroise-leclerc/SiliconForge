// Qualification matrix (ADR 0006, #27): consolidates the results of the other tools into one page per chip.
//
//   siliconforge_qualification matrix --results build/qualification --out docs/qualification
//
// Reads, in the results directory: cycles.json, differential.json, programs/*.json, and the opcodes executed by the
// test suite (coverage/tests) and by the qualification tools (coverage/qualification). Writes <chip>.md, the page,
// <chip>.matrix.json, the same data for tools, and README.md, the summary of every chip.

#include "Qualification.hpp"

#include <siliconforge/arch/avr/Opcodes.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <set>

namespace siliconforge::qualification {

namespace {

enum class State { Pass, Exception, Fail, Missing };

std::string_view symbol(State state) {
    switch (state) {
        case State::Pass: return "✅";
        case State::Exception: return "⚠️";
        case State::Fail: return "❌";
        case State::Missing: return "—";
    }
    return "?";
}
std::string_view word(State state) {
    switch (state) {
        case State::Pass: return "pass";
        case State::Exception: return "exception";
        case State::Fail: return "fail";
        case State::Missing: return "missing";
    }
    return "?";
}

State worst(std::initializer_list<State> states) {
    auto result = State::Pass;
    for (const auto state : states)
        result = std::max(result, state);
    return result;
}

struct Cell {
    State       state{State::Missing};
    std::string text;
};

std::string join(const std::set<std::string>& names) {
    std::string result;
    for (const auto& name : names)
        result += (result.empty() ? "" : ", ") + name;
    return result;
}

}  // namespace

int matrix(const Options& options) {
    const std::filesystem::path results = options.get("results", "qualification");
    const std::filesystem::path out     = options.get("out", "docs/qualification");
    const auto                  chip    = options.get("mcu", "atmega328p");
    const auto                  description = mcu::builtinDescription(chip);
    const auto                  name    = description.at("name").get<std::string>();
    const auto                  timing  = description.at("timing");

    const auto cycles       = readJson(results / "cycles.json");
    const auto differential = readJson(results / "differential.json");
    const auto tests        = avr::OpcodeCoverage::merge(results / "coverage" / "tests");
    const auto tools        = avr::OpcodeCoverage::merge(results / "coverage" / "qualification");
    std::vector<nlohmann::json> programs;
    std::error_code             error;
    for (const auto& entry : std::filesystem::directory_iterator(results / "programs", error))
        if (entry.path().extension() == ".json")
            if (auto program = readJson(entry.path()); program.is_object())
                programs.push_back(std::move(program));
    std::ranges::sort(programs, {}, [](const nlohmann::json& p) { return p.value("program", "") + p.value("level", ""); });

    // Decoding ------------------------------------------------------------------------------------------------------------
    unsigned                                   instructionOpcodes = 0, reserved = 0, elsewhere = 0;
    std::map<std::string, std::set<std::string>> aliases;
    std::map<std::string, std::string>         variants;
    std::map<std::string, unsigned>            opcodeCount, testedOpcodes, toolOpcodes;
    for (u32 op = 0; op < 65536; ++op) {
        const auto c = avr::classify(static_cast<u16>(op));
        if (c.kind == avr::OpcodeKind::Reserved) {
            ++reserved;
            elsewhere += !c.elsewhere.empty();
            continue;
        }
        ++instructionOpcodes;
        const std::string mnemonic(c.mnemonic);
        variants[mnemonic] = c.variant;
        if (!c.alias.empty())
            aliases[mnemonic].insert(std::string(c.alias));
        ++opcodeCount[mnemonic];
        testedOpcodes[mnemonic] += tests[op] != 0;
        toolOpcodes[mnemonic] += (tests[op] | tools[op]) != 0;
    }

    // One row per entry of the timing table ------------------------------------------------------------------------------
    struct Row {
        std::string mnemonic;
        Cell        decoding{}, semantics{}, flags{}, timing{}, differential{};
        State       overall{State::Missing};
    };
    std::vector<Row> rows;
    unsigned         qualified = 0, withExceptions = 0;
    for (const auto& [mnemonic, entry] : timing.items()) {
        Row row;
        row.mnemonic = mnemonic;
        const auto count = opcodeCount[mnemonic];
        row.decoding     = {count > 0 ? State::Pass : State::Fail,
                            std::format("{} opcode{}, {}{}", count, count > 1 ? "s" : "", variants[mnemonic],
                                        aliases.contains(mnemonic) ? std::format(" (alias : {})", join(aliases[mnemonic])) : "")};

        const auto* d = differential.is_object() ? &differential : nullptr;
        const auto  dm = d && d->at("mnemonics").contains(mnemonic) ? d->at("mnemonics").at(mnemonic) : nlohmann::json();
        const bool  excludedHere = d && d->at("excluded").contains(mnemonic);
        const auto  executed     = testedOpcodes[mnemonic] > 0;
        if (!dm.is_null()) {
            const auto runs = dm.at("executions").get<u64>();
            const auto ref  = dm.at("reference").get<u64>();
            row.differential = {runs > 0 ? State::Pass : State::Missing,
                                ref > 0 ? std::format("{} exécutions, {} écarts de simavr expliqués", runs, ref) : std::format("{} exécutions", runs)};
            if (ref > 0)
                row.differential.state = State::Exception;
            row.semantics = {dm.at("semantics").get<u64>() == 0 ? State::Pass : State::Fail, "différentiel"};
            row.flags     = {dm.at("flags").get<u64>() == 0 ? State::Pass : State::Fail, "différentiel"};
            if (dm.at("cycles").get<u64>() != 0)
                row.differential.state = State::Fail;
        } else if (excludedHere) {
            row.differential = {State::Exception, "exclue : " + d->at("excluded").at(mnemonic).get<std::string>()};
            row.semantics    = {executed ? State::Pass : State::Missing, executed ? "tests unitaires" : "non exercée"};
            row.flags        = {executed ? State::Pass : State::Missing, executed ? "tests unitaires" : "non exercée"};
        }
        if (cycles.is_object() && cycles.at("instructions").contains(mnemonic)) {
            const auto& c      = cycles.at("instructions").at(mnemonic);
            const auto  status = c.at("status").get<std::string>();
            std::string values;
            for (const auto& item : c.at("cases"))
                if (item.contains("measured"))
                    values += (values.empty() ? "" : " / ") + std::to_string(item.at("measured").get<int>());
            row.timing = {status == "pass" ? State::Pass : status == "exception" ? State::Exception : State::Fail, values};
        }
        row.overall = worst({row.decoding.state, row.semantics.state, row.flags.state, row.timing.state, row.differential.state});
        qualified += row.overall == State::Pass || row.overall == State::Exception;
        withExceptions += row.overall == State::Exception;
        rows.push_back(std::move(row));
    }

    unsigned passedPrograms = 0;
    for (const auto& program : programs)
        passedPrograms += program.value("status", "") == "pass";
    unsigned testedMnemonics = 0;
    for (const auto& [mnemonic, entry] : timing.items())
        testedMnemonics += testedOpcodes[mnemonic] > 0;
    const bool complete = qualified == rows.size() && passedPrograms == programs.size() && !programs.empty() && cycles.is_object() && differential.is_object();
    const auto level    = complete ? (withExceptions ? "qualifié, avec exceptions documentées" : "qualifié") : "partiellement qualifié";

    // Page ----------------------------------------------------------------------------------------------------------------
    std::filesystem::create_directories(out);
    std::ofstream page(out / (chip + ".md"));
    page << std::format("# Qualification du cœur : {}\n\n", name);
    page << "<!-- Généré par `cmake --build build --target qualification` (ADR 0006). Ne pas modifier à la main. -->\n\n";
    page << std::format("**Niveau : {}.** {} instructions sur {} qualifiées, dont {} avec une exception documentée ; "
                        "{} programmes compilés sur {} réussis.\n\n",
                        level, qualified, rows.size(), withExceptions, passedPrograms, programs.size());
    page << "## Sources de vérité\n\n";
    page << std::format("- **Décodage** : les 65 536 opcodes sont classés, {} instructions et {} réservés ({} sont des instructions "
                        "d'autres cœurs : EIJMP, ELPM, DES…). Un opcode réservé arrête l'exécution (`InvalidOpcode`). La "
                        "classification est recoupée avec le désassembleur de binutils (`avr-objdump`).\n",
                        instructionOpcodes, reserved, elsewhere);
    if (differential.is_object())
        page << std::format("- **Différentiel** : {} programmes aléatoires (graine {}, {} éléments chacun), {} instructions comparées "
                            "après chacune avec {} : registres, SREG, SP, PC, SRAM et cycles. {} divergence(s) inexpliquée(s).\n",
                            differential.at("programs").get<u64>(), differential.at("seed").get<u64>(),
                            differential.at("length").get<u64>(), differential.at("instructions").get<u64>(),
                            differential.at("reference").get<std::string>(), differential.at("unexplained").get<u64>());
    else
        page << "- **Différentiel** : non exécuté (simavr demande avr-libc et un hôte Unix).\n";
    if (cycles.is_object())
        page << std::format("- **Cycles** : {} scénarios, un par cas de la table `timing` (branchement pris, saut d'un ou deux "
                            "mots, accès SRAM ou registres), comparés à la description et à l'*{}* ; chacun des {} opcodes "
                            "exécuté une fois depuis l'état de reset.\n",
                            cycles.at("scenarios").get<u64>(), cycles.at("reference").get<std::string>(), cycles.at("opcodes").get<u64>());
    page << std::format("- **Programmes compilés** : {} variantes (`firmware/qualification/`, `-O0`, `-O2`, `-Os`) rendant leur "
                        "verdict sur les octets réservés 0xFC à 0xFF.\n",
                        programs.size());
    page << std::format("- **Couverture des tests** : {} instructions sur {} exécutées par la suite de tests unitaires et "
                        "d'intégration ; la colonne *Opcodes* compte les encodages distincts exercés par les tests, puis par "
                        "l'ensemble de la qualification.\n\n",
                        testedMnemonics, timing.size());

    page << "## Matrice\n\n";
    page << "✅ vérifié · ⚠️ exception documentée · ❌ en défaut · — non mesuré\n\n";
    page << "| Instruction | Décodage | Sémantique | Flags | Cycles | Différentiel | Opcodes (tests / qualification) |\n";
    page << "| --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& row : rows) {
        const auto cell = [](const Cell& c) { return std::format("{} {}", symbol(c.state), c.text); };
        page << std::format("| `{}` | {} | {} | {} | {} | {} | {} / {} sur {} |\n", row.mnemonic, cell(row.decoding), cell(row.semantics),
                            cell(row.flags), cell(row.timing), cell(row.differential), testedOpcodes[row.mnemonic],
                            toolOpcodes[row.mnemonic], opcodeCount[row.mnemonic]);
    }

    page << "\n## Programmes compilés\n\n| Programme | -O0 | -O2 | -Os |\n| --- | --- | --- | --- |\n";
    std::map<std::string, std::map<std::string, nlohmann::json>> byProgram;
    for (const auto& program : programs)
        byProgram[program.at("program")][program.at("level")] = program;
    for (const auto& [program, levels] : byProgram) {
        page << std::format("| `{}` |", program);
        for (const auto* optimisation : {"O0", "O2", "Os"}) {
            const auto it = levels.find(optimisation);
            if (it == levels.end()) {
                page << " — |";
                continue;
            }
            const bool ok = it->second.at("status") == "pass";
            page << std::format(" {} {} vérifications, {} cycles |", ok ? "✅" : "❌", it->second.at("checks").get<unsigned>(),
                                it->second.at("cycles").get<u64>());
        }
        page << '\n';
    }

    const auto knownList = differential.is_object() ? differential.at("knownReferenceDivergences") : nlohmann::json::array();
    std::map<std::string, std::pair<std::set<std::string>, u64>> reasons;
    for (const auto& known : knownList) {
        auto& reason = reasons[known.at("reason").get<std::string>()];
        reason.first.insert(known.at("mnemonic").get<std::string>());
        reason.second += known.at("occurrences").get<u64>();
    }
    page << "\n## Exceptions\n\n";
    for (const auto& [reason, which] : reasons)
        page << std::format("- **Écart de simavr** ({} ; {} occurrences dans cette campagne) : {}\n", join(which.first), which.second, reason);
    if (cycles.is_object()) {
        for (const auto& [mnemonic, c] : cycles.at("instructions").items())
            if (c.contains("exception"))
                page << std::format("- **Cycles de `{}`** : {}\n", mnemonic, c.at("exception").get<std::string>());
        for (const auto& stall : cycles.at("peripheralStalls"))
            page << std::format("- **`{}`** : {} cycles de plus. {}\n", stall.at("instruction").get<std::string>(),
                                stall.at("extra").get<unsigned>(), stall.at("reason").get<std::string>());
    }
    if (differential.is_object())
        for (const auto& [mnemonic, reason] : differential.at("excluded").items())
            page << std::format("- **`{}` hors différentiel** : {}\n", mnemonic, reason.get<std::string>());
    page << "\nLes encodages que le manuel déclare indéfinis (`LD r26, X+`, `ST X+, r26`…) sont exécutés de façon "
            "déterministe mais ne sont pas comparés.\n";
    page.close();

    // Data for tools, and the summary of every chip --------------------------------------------------------------------------
    nlohmann::json data{{"mcu", name}, {"level", level}, {"qualified", qualified}, {"instructions", rows.size()},
                        {"exceptions", withExceptions}, {"programs", programs.size()}, {"programsPassed", passedPrograms},
                        {"rows", nlohmann::json::array()}};
    for (const auto& row : rows)
        data["rows"].push_back({{"mnemonic", row.mnemonic}, {"decoding", word(row.decoding.state)}, {"semantics", word(row.semantics.state)},
                                {"flags", word(row.flags.state)}, {"cycles", word(row.timing.state)},
                                {"differential", word(row.differential.state)}, {"testedOpcodes", testedOpcodes[row.mnemonic]},
                                {"opcodes", opcodeCount[row.mnemonic]}});
    writeJson(out / (chip + ".matrix.json"), data);

    std::ofstream index(out / "README.md");
    index << "# Qualification des cœurs\n\n"
             "<!-- Généré par `cmake --build build --target qualification` (ADR 0006). Ne pas modifier à la main. -->\n\n"
             "Méthode : [ADR 0006](../adr/0006-qualification-coeur.md) et [guide de la qualification](../QUALIFICATION.md).\n\n"
             "| Puce | Cœur | Niveau | Instructions | Programmes compilés |\n| --- | --- | --- | --- | --- |\n";
    index << std::format("| [{}]({}.md) | {} | {} | {} / {} | {} / {} |\n", name, chip, description.at("core").get<std::string>(), level,
                         qualified, rows.size(), passedPrograms, programs.size());
    index.close();

    std::cout << std::format("{}: {} ({} / {} instructions, {} / {} programs) -> {}\n", name, level, qualified, rows.size(),
                             passedPrograms, programs.size(), (out / (chip + ".md")).string());
    return complete ? 0 : 1;
}

}  // namespace siliconforge::qualification
