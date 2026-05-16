#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <iostream>

namespace argparser {

enum class SwitchType  { FLAG, PARAMETER };
enum class Requirement { OPTIONAL, REQUIRED };

class ArgumentParserException : public std::runtime_error {
public:
    explicit ArgumentParserException(const std::string& msg)
        : std::runtime_error(msg) {}
};

class ArgumentParser {
public:
    explicit ArgumentParser(int argc, char* argv[]);
    ~ArgumentParser() = default;

    ArgumentParser(const ArgumentParser&)            = delete;
    ArgumentParser& operator=(const ArgumentParser&) = delete;
    ArgumentParser(ArgumentParser&&)                 = default;
    ArgumentParser& operator=(ArgumentParser&&)      = default;

    // Configuration
    void add_switch(std::string_view name, std::string_view description);
    void add_switch(std::string_view name, std::string_view description, SwitchType type);
    void add_switch(std::string_view name, std::string_view description,
                    SwitchType type, Requirement requirement);
    void add_switch_pair(std::string_view short_name, std::string_view long_name,
                         std::string_view description,
                         SwitchType type, Requirement requirement);

    // Parsing
    void parse();

    // Queries
    [[nodiscard]] std::string                  get_executable_name()           const;
    [[nodiscard]] bool                         is_switch_set(std::string_view) const;
    [[nodiscard]] std::optional<std::string>   get_switch_value(std::string_view) const;
    [[nodiscard]] std::optional<std::string>   get_argv_value(size_t index)    const;
    [[nodiscard]] size_t                       get_argument_count()            const;
    [[nodiscard]] const std::vector<std::string>& get_arguments()              const;

    // Help
    void print_help(std::string_view header, bool has_non_switch_arguments = false) const;

private:
    struct SwitchInfo {
        std::string            name;
        std::string            description;
        std::string            prefix;
        std::optional<std::string> value;
        std::string            pair_name;   // empty if no pair
        SwitchType             type;
        Requirement            requirement;
        bool                   is_set = false;

        SwitchInfo(std::string_view name, std::string_view description,
                   std::string_view prefix, SwitchType type, Requirement requirement);
    };

    std::string              executable_name_;
    std::vector<std::string> argv_;
    std::vector<std::string> arguments_;

    std::unordered_map<std::string, SwitchInfo>  switches_;
    // Maps pair_name → canonical name so lookup is O(1) instead of O(n)
    std::unordered_map<std::string, std::string> pair_to_canonical_;

    bool is_parsed_ = false;

    // Internal helpers
    [[nodiscard]] const SwitchInfo* find_switch(std::string_view name) const;
    [[nodiscard]] SwitchInfo*       find_switch(std::string_view name);

    [[nodiscard]] std::string format_switch_list() const;
    [[nodiscard]] std::string format_switch_line(bool has_non_switch_arguments) const;

    // Iterate unique pairs/singles, calling f(primary, paired_or_null)
    template<typename F>
    void for_each_unique_switch(F&& f) const;

    [[nodiscard]] static std::string determine_prefix(std::string_view name);
    void validate_switch_name(std::string_view name) const;

    [[nodiscard]] static bool is_switch_argument(std::string_view arg);
    size_t process_switch_argument(std::string_view arg, size_t index);
    size_t process_parameter_switch(SwitchInfo& sw, size_t index);
    void   update_paired_switch(const SwitchInfo& sw);
    void   validate_required_switches() const;
    void   handle_missing_parameter(const SwitchInfo& sw, size_t argv_index);
};

} // namespace argparser
