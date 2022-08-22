// smartifier2.cpp - This tool allows to transfer CSV data of a graph into
// smart graph format. This is version 2 with slightly different (incompatible)
// calling conventions and functionality.

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "GraphUtilsConfig.h"
#include "velocypack/Builder.h"
#include "velocypack/Iterator.h"
#include "velocypack/Parser.h"
#include "velocypack/Slice.h"
#include "velocypack/ValueType.h"
#include "velocypack/velocypack-aliases.h"

static const char USAGE[] =
    R"(Smartifier2 - transform graph data into smart graph format

    Usage:
      smartifier2 vertices --input <input>
                           --output <outputfile>
                           --smart-graph-attribute <smartgraphattr>
                           [ --type <type> ]
                           [ --write-key <bool>]
                           [ --memory <memory> ]
                           [ --smart-value <smartvalue> ]
                           [ --smart-index <smartindex> ]
                           [ --separator <separator> ]
                           [ --quote-char <quotechar> ]
                           [ --smart-default <smartdefault> ]
                           [ --randomize-smart <nr> ]
      smartifier2 edges --vertices <vertices>... 
                        --edges <edges>...
                        [ --from-attribute <fromattribute> ]
                        [ --to-attribute <toattribute> ]
                        [ --type <type> ]
                        [ --memory <memory> ]
                        [ --separator <separator> ]
                        [ --quote-char <quotechar> ]

    Options:
      --help (-h)                   Show this screen.
      --version (-v)                Show version.
      --input <input> (-i)          Input file for vertex mode.
      --output <output> (-o)        Output file for vertex mode.
      --smart-graph-attribute <smartgraphattr>  
                                    Attribute name of the smart graph attribute.
      --type <type>                 Data type "csv" or "jsonl" [default: csv]
      --write-key                   If present, the `_key` attribute will be written as
                                    it is necessary for a smart graph. If not given, the
                                    `_key` attribute is not touched or written.
      --memory <memory>             Limit RAM usage in MiB [default: 4096]
      --smart-value <smartvalue>    Attribute name to get the smart graph attribute value from.
      --smart-index <smartindex>    If given, only this many characters are taken from the 
                                    beginnin of the smart value to form
                                    the smart graph attribute value.
      --separator <separator>       Column separator for csv type [default: ,]
      --quote-char <quoteChar>      Quote character for csv type [default: "]
      --smarte-default <smartDefault>  If given, this value is taken as the value
                                    of the smart graph attribute if it is
                                    not given in a document (JSONL only)
      --randomize-smart <nr>        If given, random values are taken randomly from
                                    0 .. <nr> - 1 as smart graph attribute value,
                                    unless the attribute is already there.

    And additionally for edge mode:

      --vertices <vertices>          Vertex data in the form
                                     <collectionname>:<filename>, can be repeated.
      --edges <edges>                Edge data in the form
                                     <edgefilename>:<fromvertexcollection>:<tovertexcollection>.
)";

enum DataType { CSV = 0, JSONL = 1 };

struct Translation {
  std::unordered_map<std::string, uint32_t> keyTab;
  std::unordered_map<std::string, uint32_t> attTab;
  std::vector<std::string> smartAttributes;
  size_t memUsage = 0;  // strings in map plus table size
};

std::vector<std::string> split(std::string const& line, char sep, char quo) {
  size_t start = 0;
  size_t pos = 0;
  bool inQuote = false;
  std::vector<std::string> res;
  auto add = [&]() {
    res.push_back(line.substr(start, pos - start));
    start = ++pos;
  };
  while (pos < line.size()) {
    if (!inQuote) {
      if (line[pos] == quo) {
        inQuote = true;
        ++pos;
        continue;
      }
      if (line[pos] == sep) {
        add();
        continue;
      }
      ++pos;
    } else {  // inQuote == true
      if (line[pos] == quo) {
        if (pos + 1 < line.size() && line[pos + 1] == quo) {
          pos += 2;
          continue;
        }
        inQuote = false;
        ++pos;
        continue;
      }
      ++pos;
    }
  }
  add();
  return res;
}

int findColPos(std::vector<std::string> const& colHeaders,
               std::string const& header, std::string const& fileName) {
  auto it = std::find(colHeaders.begin(), colHeaders.end(), header);
  if (it == colHeaders.end()) {
    std::cerr << "Did not find " << header << " in column headers in file '"
              << fileName << "'" << std::endl;
    return -1;
  }
  return static_cast<int>(it - colHeaders.begin());
}

void transformEdgesCSV(Translation& translation, std::string const& vcolname,
                       std::string const& ename, char sep, char quo) {
  std::cout << "Transforming edges in " << ename << " ..." << std::endl;
  std::fstream ein(ename, std::ios_base::in);
  std::fstream eout(ename + ".out", std::ios_base::out);
  std::string line;

  // First get the header line:
  if (!getline(ein, line)) {
    std::cerr << "Could not read header line in edge file " << ename
              << std::endl;
    return;
  }
  std::vector<std::string> colHeaders = split(line, sep, quo);
  for (auto& s : colHeaders) {
    if (s.size() >= 2 && s[0] == quo && s[s.size() - 1] == quo) {
      s = s.substr(1, s.size() - 2);
    }
  }
  size_t ncols = colHeaders.size();

  // Write out header:
  eout << line << "\n";

  // Try to find the _key attribute:
  int keyPos = findColPos(colHeaders, "_key", ename);
  int fromPos = findColPos(colHeaders, "_from", ename);
  int toPos = findColPos(colHeaders, "_to", ename);
  if (fromPos < 0 || toPos < 0) {
    return;
  }
  // We tolerate -1 for the key pos!

  size_t count = 0;

  while (getline(ein, line)) {
    std::vector<std::string> parts = split(line, sep, quo);
    // Extend with empty columns to get at least the right amount of cols:
    while (parts.size() < ncols) {
      parts.emplace_back("");
    }

    auto translate = [&](int pos, std::string const& name) -> std::string {
      std::string found = parts[pos];
      bool quoted = false;
      if (found.size() > 1 && found[0] == quo &&
          found[found.size() - 1] == quo) {
        quoted = true;
        found = found.substr(1, found.size() - 2);
      }
      size_t slashpos = found.find('/');
      if (slashpos == std::string::npos) {
        // Only work if there is a slash, otherwise do not translate
        std::cerr << "Warning: found " << name << " without a slash:\n"
                  << line << "\n";
        return "";
      }
      size_t colPos = found.find(':', slashpos + 1);
      if (colPos != std::string::npos) {
        // already transformed
        return found.substr(slashpos + 1, colPos - slashpos - 1);
      }
      if (found.compare(0, slashpos, vcolname) != 0) {
        // Only work if the collection name matches the vertex collection name
        return "";
      }
      std::string key = found.substr(slashpos + 1);
      auto it = translation.keyTab.find(key);
      if (it == translation.keyTab.end()) {
        // Did not find key, simply go on
        return "";
      }
      if (quoted) {
        parts[pos] = quo + found.substr(0, slashpos + 1) +
                     translation.smartAttributes[it->second] + ":" + key + quo;
      } else {
        parts[pos] = found.substr(0, slashpos + 1) +
                     translation.smartAttributes[it->second] + ":" + key;
      }
      return translation.smartAttributes[it->second];
    };

    std::string fromAttr = translate(fromPos, "_from");
    std::string toAttr = translate(toPos, "_to");

    if (keyPos >= 0 && !fromAttr.empty() && !toAttr.empty()) {
      // See if we have to translate _key as well:
      std::string found = parts[keyPos];
      bool quoted = false;
      if (found.size() > 1 && found[0] == quo &&
          found[found.size() - 1] == quo) {
        quoted = true;
        found = found.substr(1, found.size() - 2);
      }
      size_t colPos1 = found.find(':');
      if (colPos1 == std::string::npos) {
        // both positions found, need to add both attributes:
        if (quoted) {
          parts[keyPos] = quo + fromAttr + ":" + found + ":" + toAttr + quo;
        } else {
          parts[keyPos] = fromAttr + ":" + found + ":" + toAttr;
        }
      }
    }

    // Write out the potentially modified line:
    eout << parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
      eout << ',' << parts[i];
    }
    eout << '\n';

    ++count;

    if (count % 1000000 == 0) {
      std::cout << "Have transformed " << count << " edges in " << ename
                << "..." << std::endl;
    }
  }

  std::cout << "Have transformed " << count << " edges in " << ename
            << ", finished." << std::endl;

  ein.close();
  eout.close();

  if (!eout.good()) {
    std::cerr << "An error happened at close time for " << ename + ".out"
              << ", not renaming to the original name." << std::endl;
    return;
  }

  ::unlink(ename.c_str());
  ::rename((ename + ".out").c_str(), ename.c_str());
}

void transformEdgesJSONL(Translation& translation, std::string const& vcolname,
                         std::string const& ename) {
  std::cout << "Transforming edges in " << ename << " ..." << std::endl;
  std::fstream ein(ename, std::ios_base::in);
  std::fstream eout(ename + ".out", std::ios_base::out);
  std::string line;

  size_t count = 0;

  while (getline(ein, line)) {
    // Parse line to VelocyPack:
    std::shared_ptr<VPackBuilder> b = VPackParser::fromJson(line);
    VPackSlice s = b->slice();

    auto translate = [&](std::string const& name, std::string& newValue,
                         bool& foundFlag) -> std::string {
      VPackSlice foundSlice = s.get(name);
      if (foundSlice.isNone()) {
        foundFlag = false;
        newValue = "";
        return "";
      }
      if (!foundSlice.isString()) {
        newValue.clear();
        foundFlag = true;
        return "";  // attribute not there, no transformation
      }
      foundFlag = true;
      newValue.clear();  // indicate unchanged
      std::string found = foundSlice.copyString();
      size_t slashpos = found.find('/');
      if (slashpos == std::string::npos) {
        // Only work if there is a slash, otherwise do not translate
        std::cerr << "Warning: found " << name << " without a slash:\n"
                  << line << "\n";
        return "";
      }
      size_t colPos = found.find(':', slashpos + 1);
      if (colPos != std::string::npos) {
        // already transformed
        return found.substr(slashpos + 1, colPos - slashpos - 1);
      }
      if (found.compare(0, slashpos, vcolname) != 0) {
        // Only work if the collection name matches the vertex collection name
        return "";
      }
      std::string key = found.substr(slashpos + 1);
      auto it = translation.keyTab.find(key);
      if (it == translation.keyTab.end()) {
        // Did not find key, simply go on
        return "";
      }
      newValue = found.substr(0, slashpos + 1) +
                 translation.smartAttributes[it->second] + ":" + key;
      return translation.smartAttributes[it->second];
    };

    std::string newFrom, newTo;
    bool foundFrom, foundTo;
    std::string fromAttr = translate("_from", newFrom, foundFrom);
    std::string toAttr = translate("_to", newTo, foundTo);

    std::string newKey;
    bool foundKey = false;
    VPackSlice keySlice = s.get("_key");
    if (!keySlice.isNone()) {
      foundKey = true;
    }
    if (keySlice.isString() && !fromAttr.empty() && !toAttr.empty()) {
      // See if we have to translate _key as well:
      std::string found = keySlice.copyString();
      size_t colPos1 = found.find(':');
      if (colPos1 == std::string::npos) {
        // both positions found, need to add both attributes:
        newKey = fromAttr + ":" + found + ":" + toAttr;
      }
    }

    // Write out the potentially modified line:
    bool written = false;
    eout << '{';
    auto output = [&](bool found, std::string const& name,
                      std::string const& newVal) {
      if (found) {
        if (written) {
          eout << ',';
        } else {
          written = true;
        }
        eout << '"' << name << "\":";
        if (!newVal.empty()) {
          eout << '"' << newVal << '"';
        } else {
          eout << s.get(name).toJson();
        }
      }
    };
    output(foundKey, "_key", newKey);
    output(foundFrom, "_from", newFrom);
    output(foundTo, "_to", newTo);

    for (auto const& p : VPackObjectIterator(s)) {
      std::string attrName = p.key.copyString();
      if (attrName != "_key" && attrName != "_from" && attrName != "_to") {
        if (written) {
          eout << ",";
        } else {
          written = true;
        }
        eout << "\"" << attrName << "\":" << p.value.toJson();
      }
    }
    eout << "}\n";

    ++count;

    if (count % 1000000 == 0) {
      std::cout << "Have transformed " << count << " edges in " << ename
                << "..." << std::endl;
    }
  }

  std::cout << "Have transformed " << count << " edges in " << ename
            << ", finished." << std::endl;

  ein.close();
  eout.close();

  if (!eout.good()) {
    std::cerr << "An error happened at close time for " << ename + ".out"
              << ", not renaming to the original name." << std::endl;
    return;
  }

  ::unlink(ename.c_str());
  ::rename((ename + ".out").c_str(), ename.c_str());
}

void transformVertexCSV(std::string const& line, char sep, char quo,
                        size_t ncols, int smartAttrPos, int keyPos,
                        Translation& translation, std::fstream& vout) {
  std::vector<std::string> parts = split(line, sep, quo);
  // Extend with empty columns to get at least the right amount of cols:
  while (parts.size() < ncols) {
    parts.emplace_back("");
  }

  // Store smartGraphAttribute in infrastructure if not already seen:
  std::string att = parts[smartAttrPos];
  if (att.size() >= 2 && att[0] == quo && att[att.size() - 1] == quo) {
    att = att.substr(1, att.size() - 2);
  }
  auto it = translation.attTab.find(att);
  uint32_t pos;
  if (it == translation.attTab.end()) {
    translation.smartAttributes.emplace_back(att);
    pos = static_cast<uint32_t>(translation.smartAttributes.size() - 1);
    translation.attTab.insert(std::make_pair(att, pos));
    translation.memUsage += sizeof(std::pair<std::string, uint32_t>)  // attTab
                            + att.size() + 1       // actual string
                            + sizeof(std::string)  // smartAttributes
                            + att.size() + 1;      // actual string
  } else {
    pos = it->second;
  }

  // Put the smart graph attribute into a prefix of the key, if it
  // is not already there:
  std::string key = parts[keyPos];  // Copy here temporarily!
  bool quoted = false;
  if (key.size() > 1 && key[0] == quo && key[key.size() - 1] == quo) {
    key = key.substr(1, key.size() - 2);
    quoted = true;
  }
  size_t splitPos = key.find(':');
  if (splitPos == std::string::npos) {
    // not yet transformed:
    if (quoted) {
      parts[keyPos] = quo + att + ":" + key + quo;
    } else {
      parts[keyPos] = att + ":" + key;
    }
  } else {
    key.erase(0, splitPos + 1);
  }
  it = translation.keyTab.find(key);
  if (it == translation.keyTab.end()) {
    translation.keyTab.insert(std::make_pair(key, pos));
    translation.memUsage += sizeof(std::pair<std::string, uint32_t>)  // keyTab
                            + key.size() + 1;  // actual string
  }

  // Write out the potentially modified line:
  vout << parts[0];
  for (size_t i = 1; i < parts.size(); ++i) {
    vout << ',' << parts[i];
  }
  vout << '\n';
}

void transformVertexJSONL(std::string const& line, std::string const& smartAttr,
                          Translation& translation, std::fstream& vout,
                          std::string const& smartDefault) {
  // Parse line to VelocyPack:
  std::shared_ptr<VPackBuilder> b = VPackParser::fromJson(line);
  VPackSlice s = b->slice();

  VPackSlice keySlice = s.get("_key");
  if (!keySlice.isString()) {
    vout << line << "\n";
  } else {
    std::string key = keySlice.copyString();
    std::string newKey;
    std::string att;
    size_t splitPos = key.find(':');
    uint32_t pos = 0;
    if (splitPos != std::string::npos) {
      newKey = key;
      att = key.substr(0, splitPos);
      key.erase(0, splitPos + 1);
    } else {
      // Need to transform key, where is the smart graph attribute?
      VPackSlice attSlice = s.get(smartAttr);
      if (attSlice.isString()) {
        // Store smartGraphAttribute in infrastructure if not already seen:
        att = attSlice.copyString();
        // Put the smart graph attribute into a prefix of the key:
        newKey = att + ":" + key;
      } else if (attSlice.isNone()) {
        if (!smartDefault.empty()) {
          att = smartDefault;
          newKey = att + ":" + key;
        } else {
          newKey = key;
        }
      } else {
        std::cerr << "WARNING: Vertex with non-string smart graph attribute:\n"
                  << line << ".\n";
        switch (attSlice.type()) {
          case VPackValueType::Bool:
          case VPackValueType::Double:
          case VPackValueType::UTCDate:
          case VPackValueType::Int:
          case VPackValueType::UInt:
          case VPackValueType::SmallInt:
            att = attSlice.toString();
            newKey = att + ":" + key;
            std::cerr << "WARNING: Converted to String.\n";
            break;
          default:
            newKey = key;
            std::cerr << "ERROR: Found a complextype, will not convert it.\n";
        }
      }
    }
    if (!att.empty()) {
      auto it = translation.attTab.find(att);
      if (it == translation.attTab.end()) {
        translation.smartAttributes.emplace_back(att);
        pos = static_cast<uint32_t>(translation.smartAttributes.size() - 1);
        translation.attTab.insert(std::make_pair(att, pos));
        translation.memUsage +=
            sizeof(std::pair<std::string, uint32_t>)  // attTab
            + att.size() + 1                          // actual string
            + sizeof(std::string)                     // smartAttributes
            + att.size() + 1;                         // actual string
      } else {
        pos = it->second;
      }
      it = translation.keyTab.find(key);
      if (it == translation.keyTab.end()) {
        translation.keyTab.insert(std::make_pair(key, pos));
        translation.memUsage +=
            sizeof(std::pair<std::string, uint32_t>) + key.size() + 1;
        // keyTab and actual string
      }
    }

    // Write out the potentially modified line:
    vout << R"({"_key":")" << newKey << "\"";
    for (auto const& p : VPackObjectIterator(s)) {
      std::string attrName = p.key.copyString();
      if (attrName != "_key") {
        if (attrName == smartAttr && !att.empty()) {
          // We transformed the attribute to string, write it down
          vout << ",\"" << attrName << "\":\"" << att << "\"";
        } else {
          vout << ",\"" << attrName << "\":" << p.value.toJson();
        }
      }
    }
    if (s.get(smartAttr).isNone() && !smartDefault.empty()) {
      vout << ",\"" << smartAttr << "\":\"" << smartDefault << '"';
    }
    vout << "}\n";
  }
}

#if 0
int main2(int argc, char* argv[]) {
  std::map<std::string, docopt::value> args =
      docopt::docopt(USAGE, {argv + 1, argv + argc},
                     true,  // show help if requested
                     "smartifier V" GRAPHUTILS_VERSION_MAJOR
                     "." GRAPHUTILS_VERSION_MINOR);  // version string

  std::string vname = args["<vertexFile>"].asString();
  std::string vcolname = args["<vertexColl>"].asString();
  std::string ename = args["<edgeFile>"].asString();
  std::string smartAttr = args["<smartGraphAttr>"].asString();
  std::string smartDefault;
  if (args["--smartDefault"]) {
    smartDefault = args["--smartDefault"].asString();
  }
  size_t memMB = args["--memory"].asLong();
  char sep = args["--separator"].asString()[0];
  char quo = args["--quoteChar"].asString()[0];
  DataType type = CSV;
  if (args["--type"].asString() == "jsonl") {
    type = JSONL;
  }

  std::fstream vin(vname, std::ios_base::in);
  std::string line;

  // Prepare output file for vertices:
  std::fstream vout(vname + ".out", std::ios_base::out);

  size_t ncols = 0;
  int smartAttrPos = 0;
  int keyPos = 0;
  if (type == CSV) {
    // First get the header line:
    if (!getline(vin, line)) {
      std::cerr << "Could not read header line in vertex file " << vname
                << std::endl;
      return 1;
    }
    std::vector<std::string> colHeaders = split(line, sep, quo);
    for (auto& s : colHeaders) {
      if (s.size() >= 2 && s[0] == quo && s[s.size() - 1] == quo) {
        s = s.substr(1, s.size() - 2);
      }
    }
    ncols = colHeaders.size();

    smartAttrPos = findColPos(colHeaders, smartAttr, vname);
    if (smartAttrPos < 0) {
      return 2;
    }

    keyPos = findColPos(colHeaders, "_key", vname);
    if (keyPos < 0) {
      return 3;
    }

    // Write out header:
    vout << line << "\n";
  }

  bool done = false;
  size_t count = 0;
  while (!done) {
    // We do one batch of vertices in one run of this loop
    Translation translation;
    while (!done && translation.memUsage < memMB * 1024 * 1024) {
      if (!getline(vin, line)) {
        done = true;
        break;
      }
      if (type == CSV) {
        transformVertexCSV(line, sep, quo, ncols, smartAttrPos, keyPos,
                           translation, vout);
      } else {
        transformVertexJSONL(line, smartAttr, translation, vout, smartDefault);
      }

      ++count;

      if (count % 1000000 == 0) {
        std::cout << "Have transformed " << count << " vertices, memory: "
                  << translation.memUsage / (1024 * 1024) << " MB ..."
                  << std::endl;
      }
    }
    if (count % 1000000 != 0) {
      std::cout << "Have transformed " << count
                << " vertices, memory: " << translation.memUsage / (1024 * 1024)
                << " MB ..." << std::endl;
    }
    if (type == CSV) {
      transformEdgesCSV(translation, vcolname, ename, sep, quo);
    } else {
      transformEdgesJSONL(translation, vcolname, ename);
    }
  }

  vout.close();

  if (!vout.good()) {
    std::cerr << "An error happened at close time for " << vname + ".out"
              << ", not renaming to the original name." << std::endl;
    return 5;
  }

  ::unlink(vname.c_str());
  ::rename((vname + ".out").c_str(), vname.c_str());
  return 0;
}
#endif

enum ArgType {
  Bool = 0,
  StringOnce = 3,
  StringMultiple = 4,
};

struct OptionInfoItem {
  ArgType argType;
  std::optional<std::string> defaultValue;
  std::optional<std::string> alias;
  OptionInfoItem(ArgType a) : argType(a) {}
  OptionInfoItem(ArgType a, std::string d) : argType(a), defaultValue(d) {}
  OptionInfoItem(ArgType a, std::string d, std::string al)
      : argType(a), defaultValue(d), alias(al) {}
  OptionInfoItem(ArgType a, int i, std::string al) : argType(a), alias(al) {}
};

int parseCommandLineArgs(
    char const* usage,
    std::unordered_map<std::string, OptionInfoItem> const& optionConfig,
    int argc, char* argv[],
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

int main(int argc, char* argv[]) {
  std::unordered_map<std::string, OptionInfoItem> optionConfig = {
      {"--help", OptionInfoItem(ArgType::Bool, "false", "-h")},
      {"--version", OptionInfoItem(ArgType::Bool, 0, "-v")},
      {"--type", OptionInfoItem(ArgType::StringOnce, "csv", "-t")},
      {"--input", OptionInfoItem(ArgType::StringOnce, 0, "-i")},
      {"--output", OptionInfoItem(ArgType::StringOnce, 0, "-o")},
      {"--smart-graph-attribute",
       OptionInfoItem(ArgType::StringOnce, "smart_id", "-a")},
      {"--memory", OptionInfoItem(ArgType::StringOnce, "4096", "-m")},
      {"--separator", OptionInfoItem(ArgType::StringOnce, ",", "-s")},
      {"--quote-char", OptionInfoItem(ArgType::StringOnce, "\"", "-q")},
      {"--write-key", OptionInfoItem(ArgType::Bool, "true")},
      {"--randomize-smart", OptionInfoItem(ArgType::Bool, "false")},
      {"--smart-value", OptionInfoItem(ArgType::StringOnce)},
      {"--from-attribute", OptionInfoItem(ArgType::StringOnce, "_from")},
      {"--to-attribute", OptionInfoItem(ArgType::StringOnce, "_to")},
      {"--vertices", OptionInfoItem(ArgType::StringMultiple)},
      {"--edges", OptionInfoItem(ArgType::StringMultiple)},
  };

  std::unordered_map<std::string, std::vector<std::string>> options;
  std::vector<std::string> args;
  if (parseCommandLineArgs(USAGE, optionConfig, argc, argv, options, args) !=
      0) {
    return -1;
  }
  auto it = options.find("--help");
  if (it != options.end() && it->second[0] == "true") {
    std::cout << USAGE << std::endl;
    return 0;
  }
  it = options.find("--version");
  if (it != options.end() && it->second[0] == "true") {
    std::cout << "smartifier2: Version " GRAPHUTILS_VERSION_MAJOR
                 "." GRAPHUTILS_VERSION_MINOR;  // version string
    return 0;
  }

  std::cout << "Options:\n";
  for (auto const& o : options) {
    std::cout << "  " << o.first << " ->";
    for (auto const& p : o.second) {
      std::cout << " " << p;
    }
    std::cout << "\n";
  }
  std::cout << "Arguments:\n";
  for (auto const& s : args) {
    std::cout << "  " << s << "\n";
  }
  std::cout << std::endl;
  return 0;
}
