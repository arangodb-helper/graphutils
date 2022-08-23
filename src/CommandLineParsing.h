// CommandLineParsing.h - tools to parse command lines

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum ArgType {
  Bool = 0,
  StringOnce = 3,
  StringMultiple = 4,
};

struct OptionConfigItem {
  ArgType argType;
  std::optional<std::string> defaultValue;
  std::optional<std::string> alias;
  OptionConfigItem(ArgType a) : argType(a) {}
  OptionConfigItem(ArgType a, std::string d) : argType(a), defaultValue(d) {}
  OptionConfigItem(ArgType a, std::string d, std::string al)
      : argType(a), defaultValue(d), alias(al) {}
  OptionConfigItem(ArgType a, int i, std::string al) : argType(a), alias(al) {}
};

typedef std::unordered_map<std::string, OptionConfigItem> OptionConfig;
typedef std::unordered_map<std::string, std::vector<std::string>> Options;

int parseCommandLineArgs(char const* usage, OptionConfig const& optionConfig,
                         int argc, char* argv[], Options& options,
                         std::vector<std::string>& args);

std::optional<std::vector<std::string> const*> getOption(
    Options const& options, std::string const& name);

