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

#include "CommandLineParsing.h"
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
                           [ --rename-column <nr>:<newname> ... ]
      smartifier2 edges --vertices <vertices>... 
                        --edges <edges>...
                        [ --from-attribute <fromattribute> ]
                        [ --to-attribute <toattribute> ]
                        [ --type <type> ]
                        [ --memory <memory> ]
                        [ --separator <separator> ]
                        [ --quote-char <quotechar> ]
                        [ --rename-column <nr>:<newname> ... ]

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
      --rename-column <nr>:<newname>  Before processing starts, rename column
                                    number <nr> to <newname>, only relevant for
                                    CSV, can be used multiple times, <nr> is
                                    0-based.

    And additionally for edge mode:

      --vertices <vertices>          Vertex data in the form
                                     <collectionname>:<filename>, can be repeated.
      --edges <edges>                Edge data in the form
                                     <edgefilename>:<fromvertexcollection>:<tovertexcollection>.
)";

enum DataType { CSV = 0, JSONL = 1 };

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

std::string unquote(std::string const& s, char quo) {
  std::string res;
  size_t pos = s.find(quo);
  if (pos == std::string::npos) {
    return s;
  }

  res.reserve(s.size());
  ++pos;  // now pointing to the first character after the quote
  bool inQuote = true;
  while (pos < s.size()) {
    if (inQuote) {
      if (s[pos] == quo) {
        if (pos + 1 < s.size() && s[pos + 1] == quo) {
          res.push_back(quo);
          pos += 2;
          continue;
        }
        inQuote = false;
      } else {
        res.push_back(s[pos]);
      }
    } else {  // not in quote
      if (s[pos] == quo) {
        inQuote = true;
      }
    }
    ++pos;
  }
  return res;
}

std::string quote(std::string const& s, char quo) {
  size_t pos = s.find(quo);
  if (pos == std::string::npos) {
    return s;
  }
  std::string res;
  res.reserve(s.size() + 2);  // Usually enough
  res.push_back(quo);
  for (pos = 0; pos < s.size(); ++pos) {
    if (s[pos] == quo) {
      res.push_back(quo);
      res.push_back(quo);
    } else {
      res.push_back(s[pos]);
    }
  }
  res.push_back(quo);
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

struct Translation {
  std::unordered_map<std::string, uint32_t> keyTab;
  std::unordered_map<std::string, uint32_t> attTab;
  std::vector<std::string> smartAttributes;
  size_t memUsage = 0;  // strings in map plus table size
};

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

void transformVertexCSV(std::string const& line, uint64_t count, char sep,
                        char quo, size_t ncols, int smartAttrPos,
                        int smartValuePos, int smartIndex, int keyPos,
                        std::fstream& vout) {
  std::vector<std::string> parts = split(line, sep, quo);
  // Extend with empty columns to get at least the right amount of cols:
  while (parts.size() < ncols) {
    parts.emplace_back("");
  }
  if (smartAttrPos >= ncols) {
    parts.emplace_back("");
  }
  if (keyPos >= ncols) {
    parts.emplace_back("");
  }

  // Find the smart graph attribute value, considering smart value and
  // smart index:
  std::string att;
  if (smartValuePos >= 0) {
    att = unquote(parts[smartValuePos], quo);
    if (smartIndex > 0) {
      att = att.substr(0, smartIndex);
    }
    parts[smartAttrPos] = quote(att, quo);
  } else {
    att = unquote(parts[smartAttrPos], quo);
  }

  // Put the smart graph attribute into a prefix of the key, if it
  // is not already there:
  std::string key = unquote(parts[keyPos], quo);  // Copy here temporarily!
  size_t splitPos = key.find(':');
  if (splitPos == std::string::npos) {
    // not yet transformed:
    parts[keyPos] = quote(att + ":" + key, quo);
  } else {
    if (key.substr(0, splitPos) != att) {
      std::cerr << "Found wrong key w.r.t. smart graph attribute: " << key
                << " smart graph attribute is " << att << " in line " << count
                << std::endl;
      parts[keyPos] = quote(att + ":" + key.substr(splitPos + 1), quo);
    }
  }

  // Write out the potentially modified line:
  vout << parts[0];
  for (size_t i = 1; i < parts.size(); ++i) {
    vout << sep << parts[i];
  }
  vout << '\n';
}

std::string smartToString(VPackSlice attSlice, std::string const& smartDefault,
                          size_t count) {
  if (attSlice.isString()) {
    return attSlice.copyString();
  } else if (attSlice.isNone()) {
    if (!smartDefault.empty()) {
      return smartDefault;
    }
  } else {
    std::cerr << "WARNING: Vertex with non-string smart graph attribute:\n"
              << count << ".\n";
    switch (attSlice.type()) {
      case VPackValueType::Bool:
      case VPackValueType::Double:
      case VPackValueType::UTCDate:
      case VPackValueType::Int:
      case VPackValueType::UInt:
      case VPackValueType::SmallInt:
        return attSlice.toString();
        std::cerr << "WARNING: Converted to String.\n";
        break;
      default:
        std::cerr << "ERROR: Found a complextype, will not convert it.\n";
    }
  }
  return "";
}

void transformVertexJSONL(std::string const& line, size_t count,
                          std::string const& smartAttr, std::string smartValue,
                          int smartIndex, std::string const& smartDefault,
                          std::fstream& vout) {
  // Parse line to VelocyPack:
  std::shared_ptr<VPackBuilder> b = VPackParser::fromJson(line);
  VPackSlice s = b->slice();

  // First derive the smart graph attribute value:
  std::string att;
  if (!smartValue.empty()) {
    VPackSlice valSlice = s.get(smartValue);
    att = smartToString(valSlice, smartDefault, count);
    if (smartIndex > 0) {
      att = att.substr(0, smartIndex);
    }
  }

  if (att.empty()) {
    // Need to lookup smart graph attribute itself:
    VPackSlice attSlice = s.get(smartAttr);
    att = smartToString(attSlice, smartDefault, count);
  }

  // Now consider the _key:
  VPackSlice keySlice = s.get("_key");
  if (!keySlice.isString()) {
    vout << line << "\n";
    return;
  }

  std::string key = keySlice.copyString();
  std::string newKey;
  size_t splitPos = key.find(':');
  uint32_t pos = 0;
  if (splitPos != std::string::npos) {
    newKey = key;
    if (att != key.substr(0, splitPos)) {
      std::cerr << "_key is already smart, but with the wrong smart graph "
                   "attribute:\n"
                << line << "\n";
      vout << line << "\n";
      return;
    }
  } else {
    if (!att.empty()) {
      newKey = att + ":" + key;
    } else {
      newKey = key;
    }
  }

  // Write out the potentially modified line:
  vout << R"({"_key":")" << newKey << R"(",")" << smartAttr << R"(":")" << att
       << '"';
  for (auto const& p : VPackObjectIterator(s)) {
    std::string attrName = p.key.copyString();
    if (attrName != "_key" && attrName != smartAttr) {
      vout << ",\"" << attrName << "\":" << p.value.toJson();
    }
  }
  vout << "}\n";
}

void renameColumns(Options const& options,
                   std::vector<std::string>& colHeaders) {
  auto it = options.find("--rename-column");
  if (it != options.end()) {
    for (auto const& s : it->second) {
      auto pos = s.find(':');
      if (pos != std::string::npos) {
        size_t nr = strtoul(s.substr(0, pos).c_str(), nullptr, 10);
        if (nr < colHeaders.size() && pos + 1 < s.size()) {
          colHeaders[nr] = s.substr(pos + 1);
        }
      }
    }
  }
}

void doVertices(Options const& options) {
  auto input = getOption(options, "--input");
  if (!input) {
    std::cerr << "Need input file with --input option, giving up." << std::endl;
    return;
  }
  auto output = getOption(options, "--output");
  if (!output) {
    std::cerr << "Need output file with --output option, giving up."
              << std::endl;
    return;
  }
  std::string inputFile = (*input.value())[0];
  std::string outputFile = (*output.value())[0];
  std::string smartAttr =
      (*getOption(options, "--smart-graph-attribute").value())[0];
  bool haveSmartValue = false;
  std::string smartValue;
  int64_t smartIndex = -1;  // does not count
  auto it = options.find("--smart-value");
  if (it != options.end()) {
    smartValue = it->second[0];
    haveSmartValue = true;
    it = options.find("--smart-index");
    if (it != options.end()) {
      smartIndex = strtol(it->second[0].c_str(), nullptr, 10);
    }
  }
  DataType type = CSV;
  it = options.find("--type");
  if (it != options.end()) {
    if (it->second[0] == "jsonl" || it->second[0] == "JSONL") {
      type = JSONL;
    }
  }
  char sep = ',';
  it = options.find("--separator");
  if (it != options.end() && !it->second[0].empty()) {
    sep = it->second[0][0];
  }
  char quo;
  it = options.find("--quote-char");
  if (it != options.end() && !it->second[0].empty()) {
    quo = it->second[0][0];
  }

  // Only for JSONL:
  std::string smartDefault = "";

  // Input file:
  std::fstream vin(inputFile, std::ios_base::in);
  std::string line;

  // Prepare output file for vertices:
  std::fstream vout(outputFile, std::ios_base::out);

  size_t ncols = 0;
  int smartAttrPos = -1;
  int smartValuePos = -1;
  int keyPos = -1;
  if (type == CSV) {
    // First get the header line:
    if (!getline(vin, line)) {
      std::cerr << "Could not read header line in vertex file " << inputFile
                << std::endl;
      return;
    }
    std::vector<std::string> colHeaders = split(line, sep, quo);
    for (auto& s : colHeaders) {
      s = unquote(s, quo);
    }
    ncols = colHeaders.size();

    // Potentially rename columns:
    renameColumns(options, colHeaders);

    bool newSmartColumn = false;
    smartAttrPos = findColPos(colHeaders, smartAttr, inputFile);
    if (smartAttrPos < 0) {
      smartAttrPos = colHeaders.size();
      colHeaders.push_back(smartAttr);
      newSmartColumn = true;
    }

    if (haveSmartValue) {
      smartValuePos = findColPos(colHeaders, smartValue, inputFile);
      if (smartValuePos < 0) {
        std::cerr << "Warning: Could not find column for smart value. "
                     "Ignoring..."
                  << std::endl;
      }
    }

    bool newKeyColumn = false;
    keyPos = findColPos(colHeaders, "_key", inputFile);
    if (keyPos < 0) {
      keyPos = ncols;
      colHeaders.push_back("_key");
      newKeyColumn = true;
    }

    // Write out header:
    bool first = true;
    for (auto const& h : colHeaders) {
      if (!first) {
        vout << sep;
      }
      vout << quote(h, quo);
      first = false;
    }
    vout << "\n";
  } else {
    it = options.find("--smart-default");
    if (it != options.end()) {
      smartDefault = it->second[0];
    }
  }

  size_t count = 1;
  while (true) {
    if (!getline(vin, line)) {
      break;
    }
    if (type == CSV) {
      transformVertexCSV(line, count + 1, sep, quo, ncols, smartAttrPos,
                         smartValuePos, smartIndex, keyPos, vout);
    } else {
      transformVertexJSONL(line, count, smartAttr, smartValue, smartIndex,
                           smartDefault, vout);
    }

    ++count;

    if (count % 1000000 == 0) {
      std::cout << "Have transformed " << count << " vertices." << std::endl;
    }
  }

  vout.close();

  if (!vout.good()) {
    std::cerr << "An error happened at close time for " << outputFile << "."
              << std::endl;
    return;
  }
}

struct VertexBuffer {
 public:
  std::vector<std::string> _vertexFiles;
  std::vector<std::string> _vertexCollNames;

 private:
  Translation _trans;
  size_t _filePos;
  std::ifstream _currentInput;
  bool _fileOpen;
  DataType _type;

  VertexBuffer(DataType type) : _filePos(0), _fileOpen(false), _type(type) {}
  bool isDone() { return _filePos >= _vertexFiles.size(); }
  void readMore() {}
};

void doEdges(Options const& options) {
  // Check options, find vertex colls and edge colls
  // Set up translator and set up vertex reader object
  // while vertex reader object not done
  //   run through all edge collections, one at a time
  //     transform what can be done, write to tmp file
  //     move tmp file to original file
  //   forget all vertex data
  //   read more vertex data
}

#define MYASSERT(t)                                         \
  if (!(t)) {                                               \
    std::cerr << "Error in line " << __LINE__ << std::endl; \
  }

void runTests() {
  std::string s = quote("abc", '"');
  MYASSERT(s == "abc");
  s = quote("a\"b\"c", '"');
  MYASSERT(s == "\"a\"\"b\"\"c\"");
}

int main(int argc, char* argv[]) {
  OptionConfig optionConfig = {
      {"--help", OptionConfigItem(ArgType::Bool, "false", "-h")},
      {"--version", OptionConfigItem(ArgType::Bool, 0, "-v")},
      {"--test", OptionConfigItem(ArgType::Bool, "false")},
      {"--type", OptionConfigItem(ArgType::StringOnce, "csv", "-t")},
      {"--input", OptionConfigItem(ArgType::StringOnce, 0, "-i")},
      {"--output", OptionConfigItem(ArgType::StringOnce, 0, "-o")},
      {"--smart-graph-attribute",
       OptionConfigItem(ArgType::StringOnce, "smart_id", "-a")},
      {"--memory", OptionConfigItem(ArgType::StringOnce, "4096", "-m")},
      {"--separator", OptionConfigItem(ArgType::StringOnce, ",", "-s")},
      {"--quote-char", OptionConfigItem(ArgType::StringOnce, "\"", "-q")},
      {"--write-key", OptionConfigItem(ArgType::Bool, "true")},
      {"--randomize-smart", OptionConfigItem(ArgType::Bool, "false")},
      {"--smart-value", OptionConfigItem(ArgType::StringOnce)},
      {"--smart-index", OptionConfigItem(ArgType::StringOnce)},
      {"--from-attribute", OptionConfigItem(ArgType::StringOnce, "_from")},
      {"--to-attribute", OptionConfigItem(ArgType::StringOnce, "_to")},
      {"--vertices", OptionConfigItem(ArgType::StringMultiple)},
      {"--edges", OptionConfigItem(ArgType::StringMultiple)},
      {"--rename-column", OptionConfigItem(ArgType::StringMultiple)},
      {"--smart-default", OptionConfigItem(ArgType::StringOnce)},
  };

  Options options;
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
  it = options.find("--test");
  if (it != options.end() && it->second[0] == "true") {
    std::cout << "Running unit tests...\n";
    runTests();
    std::cout << "Done." << std::endl;
    return 0;
  }

  if (args.size() != 1 || (args[0] != "vertices" && args[0] != "edges")) {
    std::cerr << "Need exactly one subcommand 'vertices' or 'edges'.\n";
    return -2;
  }

  if (args[0] == "vertices") {
    doVertices(options);
  } else if (args[0] == "edges") {
    doEdges(options);
  }

  return 0;
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

