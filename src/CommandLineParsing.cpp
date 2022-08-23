// CommandLineParsing.cpp - tools for command line parsing

#include "CommandLineParsing.h"

#include <iostream>

int parseCommandLineArgs(
    char const* usage, OptionConfig const& optionConfig, int argc, char* argv[],
    std::unordered_map<std::string, std::vector<std::string>>& options,
    std::vector<std::string>& args) {
  // Build alias lookup:
  std::unordered_map<std::string, std::string> aliases;
  for (auto const& p : optionConfig) {
    if (p.second.alias) {
      aliases.emplace(p.second.alias.value(), p.first);
    }
  }

  options.clear();
  args.clear();
  bool pastOptions = false;
  for (size_t i = 1; i < argc; ++i) {
    std::string a{argv[i]};
    std::string b;
    if (a[0] == '-' && a.size() > 0 && !pastOptions) {  // seems to be an option
      if (a == "--") {
        pastOptions = true;
      } else {
        // Recognize = sign in option:
        auto pos = a.find('=');
        bool advanced = false;
        if (pos != std::string::npos) {
          b = a.substr(pos + 1);
          a = a.substr(0, pos);
        } else {
          if (i + 1 < argc) {
            b = argv[i + 1];
            ++i;
            advanced = true;
          }
        }

        // Use aliases:
        auto it = aliases.find(a);
        if (it != aliases.end()) {
          a = it->second;
        }

        // Find option in config:
        auto it2 = optionConfig.find(a);
        if (it2 == optionConfig.end()) {
          std::cerr << "Unknown option '" << a << "', giving up.\n"
                    << usage << std::endl;
          return 1;
        }

        // Check rules:
        ArgType at = it2->second.argType;

        // Check value:
        if (at == Bool) {
          if (b == "false" || b == "FALSE" || b == "False" || b == "No" ||
              b == "NO" || b == "no" || b == "f" || b == "F" || b == "n" ||
              b == "N") {
            b = "false";
          } else if (b == "true" || b == "TRUE" || b == "True" || b == "Yes" ||
                     b == "YES" || b == "yes" || b == "t" || b == "T" ||
                     b == "y" || b == "Y") {
            b = "true";
          } else {  // Do not accept this argument
            b = "true";
            if (advanced) {
              --i;
            }
          }
        }

        // Now set value:
        auto it3 = options.find(a);
        if (it3 == options.end()) {
          options.emplace(a, std::vector<std::string>{b});
        } else {
          if (at == ArgType::StringOnce || at == ArgType::Bool) {
            std::cerr << "Option '" << a
                      << "' must only occur once, giving up.\n"
                      << usage << std::endl;
            return 2;
          }
          it3->second.push_back(b);
        }
      }
    } else {
      args.push_back(a);
    }
  }

  // Finally, set defaults:
  for (auto const& p : optionConfig) {
    if (p.second.defaultValue) {
      auto it = options.find(p.first);
      if (it == options.end()) {
        options.emplace(
            p.first, std::vector<std::string>{p.second.defaultValue.value()});
      }
    }
  }
  return 0;
}

std::optional<std::vector<std::string> const*> getOption(
    Options const& options, std::string const& name) {
  auto it = options.find(name);
  if (it == options.end()) {
    return {};
  }
  return {&it->second};
}
