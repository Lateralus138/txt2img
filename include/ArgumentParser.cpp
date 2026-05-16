#include "ArgumentParser.h"
#include <filesystem>
#include <unordered_set>

namespace argparser {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ArgumentParser::ArgumentParser(int argc, char* argv[]) {
    if (argc < 1 || !argv[0])
        throw ArgumentParserException("Invalid command line arguments");

    executable_name_ = std::filesystem::path(argv[0]).filename().string();

    argv_.reserve(argc - 1);
    for (int i = 1; i < argc; ++i)
        if (argv[i]) argv_.emplace_back(argv[i]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Switch registration
// ─────────────────────────────────────────────────────────────────────────────

void ArgumentParser::add_switch(std::string_view name, std::string_view description) {
    add_switch(name, description, SwitchType::FLAG, Requirement::OPTIONAL);
}

void ArgumentParser::add_switch(std::string_view name, std::string_view description,
                                SwitchType type) {
    add_switch(name, description, type, Requirement::OPTIONAL);
}

void ArgumentParser::add_switch(std::string_view name, std::string_view description,
                                SwitchType type, Requirement requirement) {
    validate_switch_name(name);
    const std::string n(name);
    switches_.emplace(n, SwitchInfo(n, std::string(description),
                                    determine_prefix(name), type, requirement));
}

void ArgumentParser::add_switch_pair(std::string_view short_name, std::string_view long_name,
                                     std::string_view description,
                                     SwitchType type, Requirement requirement) {
    validate_switch_name(short_name);
    validate_switch_name(long_name);

    const std::string sn(short_name), ln(long_name), desc(description);

    switches_.emplace(sn, SwitchInfo(sn, desc, determine_prefix(short_name), type, requirement));
    switches_.emplace(ln, SwitchInfo(ln, desc, determine_prefix(long_name),  type, requirement));

    switches_.at(sn).pair_name = ln;
    switches_.at(ln).pair_name = sn;

    // Register reverse-lookup entries so is_switch_set / get_switch_value
    // can resolve aliases in O(1).
    pair_to_canonical_[ln] = sn;  // long  → short (canonical = short)
    pair_to_canonical_[sn] = ln;  // short → long  (and vice versa for completeness)
}

// ─────────────────────────────────────────────────────────────────────────────
// O(1) switch lookup (replaces the O(n) linear scan in the original)
// ─────────────────────────────────────────────────────────────────────────────

const ArgumentParser::SwitchInfo* ArgumentParser::find_switch(std::string_view name) const {
    const std::string n(name);
    auto it = switches_.find(n);
    if (it != switches_.end()) return &it->second;

    // Try alias
    auto alias = pair_to_canonical_.find(n);
    if (alias != pair_to_canonical_.end()) {
        auto it2 = switches_.find(alias->second);
        if (it2 != switches_.end()) return &it2->second;
    }
    return nullptr;
}

ArgumentParser::SwitchInfo* ArgumentParser::find_switch(std::string_view name) {
    return const_cast<SwitchInfo*>(
        static_cast<const ArgumentParser*>(this)->find_switch(name));
}

// ─────────────────────────────────────────────────────────────────────────────
// Parsing
// ─────────────────────────────────────────────────────────────────────────────

void ArgumentParser::parse() {
    if (is_parsed_) return;

    arguments_.clear();
    for (size_t i = 0; i < argv_.size(); ++i) {
        if (is_switch_argument(argv_[i]))
            i = process_switch_argument(argv_[i], i);
        else
            arguments_.emplace_back(argv_[i]);
    }

    validate_required_switches();
    is_parsed_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public query methods
// ─────────────────────────────────────────────────────────────────────────────

std::string ArgumentParser::get_executable_name() const { return executable_name_; }

bool ArgumentParser::is_switch_set(std::string_view name) const {
    const auto* sw = find_switch(name);
    return sw && sw->is_set;
}

std::optional<std::string> ArgumentParser::get_switch_value(std::string_view name) const {
    const auto* sw = find_switch(name);
    return sw ? sw->value : std::nullopt;
}

std::optional<std::string> ArgumentParser::get_argv_value(size_t index) const {
    return index < argv_.size() ? std::optional<std::string>(argv_[index]) : std::nullopt;
}

size_t ArgumentParser::get_argument_count() const { return arguments_.size(); }

const std::vector<std::string>& ArgumentParser::get_arguments() const { return arguments_; }

// ─────────────────────────────────────────────────────────────────────────────
// Help output
// ─────────────────────────────────────────────────────────────────────────────

void ArgumentParser::print_help(std::string_view header, bool has_non_switch_arguments) const {
    std::cout << "\n" << header << "\n\n"
              << "Usage:\n  " << executable_name_ << " "
              << format_switch_line(has_non_switch_arguments) << "\n\n"
              << "Switches:\n" << format_switch_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Unique-switch iteration — shared by format_switch_list and format_switch_line
// to eliminate duplicated deduplication logic.
// ─────────────────────────────────────────────────────────────────────────────

template<typename F>
void ArgumentParser::for_each_unique_switch(F&& f) const {
    std::unordered_set<std::string> seen;
    seen.reserve(switches_.size());

    for (const auto& [name, sw] : switches_) {
        if (seen.count(name)) continue;
        if (!sw.pair_name.empty() && seen.count(sw.pair_name)) continue;

        // Identify the short and (optional) long side
        const SwitchInfo* short_sw = nullptr;
        const SwitchInfo* long_sw  = nullptr;

        if (!sw.pair_name.empty()) {
            const auto& paired = switches_.at(sw.pair_name);
            if (sw.prefix == "-") { short_sw = &sw;     long_sw = &paired; }
            else                   { short_sw = &paired; long_sw = &sw;    }
        } else {
            short_sw = &sw; // single switch — use "short" slot
        }

        f(short_sw, long_sw);   // long_sw may be nullptr for unpaired switches

        seen.insert(name);
        if (!sw.pair_name.empty()) seen.insert(sw.pair_name);
    }
}

std::string ArgumentParser::format_switch_list() const {
    std::string result;

    for_each_unique_switch([&](const SwitchInfo* primary, const SwitchInfo* secondary) {
        if (secondary) {
            result += "  " + primary->prefix + primary->name
                    + ", " + secondary->prefix + secondary->name;
        } else {
            result += "  " + primary->prefix + primary->name;
        }

        if (primary->type == SwitchType::PARAMETER) result += " <parameter>";
        result += "\n  " + primary->description;
        if (primary->requirement == Requirement::REQUIRED) result += " (Required)";
        result += "\n";
    });

    return result;
}

std::string ArgumentParser::format_switch_line(bool has_non_switch_arguments) const {
    const std::string pad(executable_name_.size() + 3, ' ');
    std::string result;
    bool first = true;

    for_each_unique_switch([&](const SwitchInfo* primary, const SwitchInfo* secondary) {
        if (!first) result += " |\n" + pad;
        first = false;

        result += "[" + primary->prefix + primary->name;
        if (secondary) result += " | " + secondary->prefix + secondary->name;
        if (primary->type == SwitchType::PARAMETER) result += " <parameter>";
        result += "]";
    });

    if (has_non_switch_arguments)
        result += (result.empty() ? "" : "\n" + pad) + "<ARGUMENTS>";

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private parsing helpers
// ─────────────────────────────────────────────────────────────────────────────

ArgumentParser::SwitchInfo::SwitchInfo(std::string_view name, std::string_view description,
                                       std::string_view prefix, SwitchType type, Requirement req)
    : name(name), description(description), prefix(prefix), type(type), requirement(req) {}

// static
std::string ArgumentParser::determine_prefix(std::string_view name) {
    return (name.size() == 1) ? "-" : "--";
}

void ArgumentParser::validate_switch_name(std::string_view name) const {
    if (name.empty())
        throw ArgumentParserException("Switch name cannot be empty");
    if (switches_.count(std::string(name)))
        throw ArgumentParserException("Switch '" + std::string(name) + "' already exists");
}

// static
bool ArgumentParser::is_switch_argument(std::string_view arg) {
    return arg.size() > 1 && arg[0] == '-';
}

size_t ArgumentParser::process_switch_argument(std::string_view arg, size_t index) {
    for (auto& [name, sw] : switches_) {
        if (arg == sw.prefix + sw.name) {
            sw.is_set = true;
            if (sw.type == SwitchType::PARAMETER)
                return process_parameter_switch(sw, index);
            update_paired_switch(sw);
            return index;
        }
    }
    arguments_.emplace_back(arg); // unknown switch → treat as positional argument
    return index;
}

size_t ArgumentParser::process_parameter_switch(SwitchInfo& sw, size_t index) {
    if (index + 1 < argv_.size() && !is_switch_argument(argv_[index + 1])) {
        sw.value = argv_[index + 1];
        update_paired_switch(sw);
        return index + 1;
    }
    handle_missing_parameter(sw, index);
    return index;
}

void ArgumentParser::update_paired_switch(const SwitchInfo& sw) {
    if (!sw.pair_name.empty()) {
        auto& paired   = switches_.at(sw.pair_name);
        paired.is_set  = true;
        paired.value   = sw.value;
    }
}

void ArgumentParser::validate_required_switches() const {
    for (const auto& [name, sw] : switches_) {
        if (sw.requirement == Requirement::REQUIRED && !sw.is_set)
            throw ArgumentParserException(
                "Required switch '" + sw.prefix + sw.name + "' is not set");
    }
}

void ArgumentParser::handle_missing_parameter(const SwitchInfo& sw, size_t /*argv_index*/) {
    if (sw.requirement == Requirement::REQUIRED)
        throw ArgumentParserException(
            "Missing required parameter for switch '" + sw.prefix + sw.name + "'");
    // Optional with no value — leave sw.value as nullopt
}

} // namespace argparser
