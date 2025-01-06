// smartifier2.cpp - This tool allows to transfer CSV data of a graph into
// smart graph format. This is version 2 with slightly different (incompatible)
// calling conventions and functionality.

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <openssl/evp.h>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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

std::chrono::steady_clock::time_point startTime;

std::string calculateSha1(const std::string &input) {
  EVP_MD_CTX *context = EVP_MD_CTX_new();
  if (context == nullptr) {
    throw std::runtime_error("Failed to create EVP context");
  }

  if (EVP_DigestInit_ex(context, EVP_sha1(), nullptr) != 1) {
    EVP_MD_CTX_free(context);
    throw std::runtime_error("Failed to initialize digest");
  }

  if (EVP_DigestUpdate(context, input.c_str(), input.length()) != 1) {
    EVP_MD_CTX_free(context);
    throw std::runtime_error("Failed to update digest");
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int lengthOfHash = 0;

  if (EVP_DigestFinal_ex(context, hash, &lengthOfHash) != 1) {
    EVP_MD_CTX_free(context);
    throw std::runtime_error("Failed to finalize digest");
  }

  EVP_MD_CTX_free(context);

  std::stringstream ss;
  for (unsigned int i = 0; i < lengthOfHash; i++) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash[i]);
  }

  return ss.str();
}

double elapsed() {
  auto now = std::chrono::steady_clock::now();
  auto diff = now - startTime;
  return diff.count() / 1e9;
}

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
                           [ --hash-smart-value <bool> ]
                           [ --separator <separator> ]
                           [ --quote-char <quotechar> ]
                           [ --smart-default <smartdefault> ]
                           [ --randomize-smart <nr> ]
                           [ --rename-column <nr>:<newname> ... ]
                           [ --key-value <name> ]
      smartifier2 edges --vertices <vertices>... 
                        --edges <edges>...
                        [ --from-attribute <fromattribute> ]
                        [ --to-attribute <toattribute> ]
                        [ --type <type> ]
                        [ --memory <memory> ]
                        [ --separator <separator> ]
                        [ --quote-char <quotechar> ]
                        [ --smart-index <index> ]
                        [ --threads <nrthreads> ]

    Options:
      --help (-h)                   Show this screen.
      --version (-v)                Show version.
      --input <input> (-i)          Input file for vertex mode.
      --output <output> (-o)        Output file for vertex mode.
      --smart-graph-attribute <smartgraphattr>  
                                    Attribute name of the smart graph attribute.
      --type <type>                 Data type "csv" or "jsonl" [default: csv]
      --write-key                   If present, the `_key` attribute will be
                                    written as it is necessary for a
                                    smart graph. If not given, the
                                    `_key` attribute is not touched or
                                    written.
      --memory <memory>             Limit RAM usage in MiB [default: 4096]
      --smart-value <smartvalue>    Attribute name to get the smart graph
                                    attribute value from.
      --smart-index <smartindex>    If given, only this many characters are
                                    taken from the beginning of the
                                    smart value to form the smart graph
                                    attribute value.
      --separator <separator>       Column separator for csv type [default: ,]
      --quote-char <quoteChar>      Quote character for csv type [default: "]
      --smart-default <smartDefault>  If given, this value is taken as the value
                                    of the smart graph attribute if it is
                                    not given in a document (JSONL only)
      --randomize-smart <nr>        If given, random values are taken randomly
                                    from 0 .. <nr> - 1 as smart graph
                                    attribute value, unless the
                                    attribute is already there.
      --rename-column <nr>:<newname>  Before processing starts, rename column
                                    number <nr> to <newname>, only relevant for
                                    CSV, can be used multiple times, <nr> is
                                    0-based.
      --key-value <name>            Take the key value from the column/attribute
                                    named <name>. The `_key` column/attribute
                                    will be built using the smart graph
                                    attribute value, a colon and the value
                                    of the column/attribute named here.

    And additionally for edge mode:

      --vertices <vertices>          Vertex data in the form
            <collectionname>:<filename>, can be repeated.
      --edges <edges>                Edge data in the form
            <edgefilename>:<fromvertexcollection>:<tovertexcollection>.
          If needed, append :<columnnumber>:<newcolumnname> pairs to rename
          columns before processing.
      --smart-index <index>          If given here, no vertex data must be
                                     given, and the smart graph attribute
                                     will be the first <index> characters
                                     of the key, so we can transform _from
                                     and _to locally.
      --threads <nrthreads>          Number of threads to use, only relevant
                                     when multiple edge files are given.
)";

enum DataType { CSV = 0, JSONL = 1 };

std::vector<std::string> split(std::string const &line, char sep, char quo) {
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
    } else { // inQuote == true
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

std::string unquote(std::string const &s, char quo) {
  std::string res;
  size_t pos = s.find(quo);
  if (pos == std::string::npos) {
    return s;
  }

  res.reserve(s.size());
  ++pos; // now pointing to the first character after the quote
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
    } else { // not in quote
      if (s[pos] == quo) {
        inQuote = true;
      }
    }
    ++pos;
  }
  return res;
}

std::string quote(std::string const &s, char quo) {
  size_t pos = s.find(quo);
  if (pos == std::string::npos) {
    return s;
  }
  std::string res;
  res.reserve(s.size() + 2); // Usually enough
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

int findColPos(std::vector<std::string> const &colHeaders,
               std::string const &header, std::string const &fileName) {
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
  size_t memUsage = 0; // strings in map plus table size
  void clear() {
    keyTab.clear();
    attTab.clear();
    smartAttributes.clear();
    memUsage = 0;
  }
};

struct EdgeCollection {
  std::string fileName;
  std::string fromVertColl;
  std::string toVertColl;
  std::vector<std::pair<int, std::string>> columnRenames;
};

void transformVertexCSV(std::string const &line, uint64_t count, char sep,
                        char quo, size_t ncols, int smartAttrPos,
                        int smartValuePos, int smartIndex, bool hashSmartValue,
                        int keyPos, int keyValuePos, std::fstream &vout) {
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
    if (hashSmartValue) {
      att = calculateSha1(att);
    }
    if (smartIndex > 0) {
      att = att.substr(0, smartIndex);
    }
    parts[smartAttrPos] = quote(att, quo);
  } else {
    att = unquote(parts[smartAttrPos], quo);
  }

  // Put the smart graph attribute into a prefix of the key, if it
  // is not already there:
  std::string key;
  if (keyValuePos >= 0) {
    key = unquote(parts[keyValuePos], quo);
  } else {
    key = unquote(parts[keyPos], quo); // Copy here temporarily!
  }
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

std::string smartToString(VPackSlice attSlice, std::string const &smartDefault,
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

void transformVertexJSONL(std::string const &line, size_t count,
                          std::string const &smartAttr, std::string smartValue,
                          int smartIndex, bool hashSmartValue,
                          std::string const &smartDefault, bool writeKey,
                          std::string const &keyValue, std::fstream &vout) {
  // Parse line to VelocyPack:
  std::shared_ptr<VPackBuilder> b = VPackParser::fromJson(line);
  VPackSlice s = b->slice();

  // First derive the smart graph attribute value:
  std::string att;
  if (!smartValue.empty()) {
    VPackSlice valSlice = s.get(smartValue);
    att = smartToString(valSlice, smartDefault, count);
    if (hashSmartValue) {
      att = calculateSha1(att);
    }
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
  std::string newKey;
  VPackSlice keySlice;
  if (!keyValue.empty()) {
    keySlice = s.get(keyValue);
  } else {
    keySlice = s.get("_key");
  }
  if (keySlice.isString()) {
    std::string key = keySlice.copyString();
    size_t splitPos = key.find(':');
    uint32_t pos = 0;
    if (splitPos != std::string::npos) {
      newKey = key;
      if (att != key.substr(0, splitPos)) {
        std::cerr << "_key is already smart, but with the wrong smart graph "
                     "attribute:\n"
                  << line << "\n";
      }
    } else {
      if (!att.empty()) {
        newKey = att + ":" + key;
      } else {
        newKey = key;
      }
    }
  }

  // Write out the potentially modified line:
  vout << "{";
  if (writeKey || !newKey.empty()) {
    vout << R"("_key":")" << newKey << R"(",")";
  }
  vout << smartAttr << R"(":")" << att << '"';
  for (auto const &p : VPackObjectIterator(s)) {
    std::string attrName = p.key.copyString();
    if (attrName != "_key" && attrName != smartAttr) {
      vout << ",\"" << attrName << "\":" << p.value.toJson();
    }
  }
  vout << "}\n";
}

void renameColumns(Options const &options,
                   std::vector<std::string> &colHeaders) {
  auto it = options.find("--rename-column");
  if (it != options.end()) {
    for (auto const &s : it->second) {
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

int doVertices(Options const &options) {
  auto input = getOption(options, "--input");
  if (!input) {
    std::cerr << "Need input file with --input option, giving up." << std::endl;
    return 1;
  }
  auto output = getOption(options, "--output");
  if (!output) {
    std::cerr << "Need output file with --output option, giving up."
              << std::endl;
    return 2;
  }
  std::string inputFile = (*input.value())[0];
  std::string outputFile = (*output.value())[0];
  std::string smartAttr =
      (*getOption(options, "--smart-graph-attribute").value())[0];
  bool haveSmartValue = false;
  std::string smartValue;
  int64_t smartIndex = -1; // does not count
  bool hashSmartValue = false;
  auto it = options.find("--smart-value");
  if (it != options.end()) {
    smartValue = it->second[0];
    haveSmartValue = true;
    it = options.find("--smart-index");
    if (it != options.end()) {
      smartIndex = strtol(it->second[0].c_str(), nullptr, 10);
    }
    it = options.find("--hash-smart-value");
    if (it != options.end()) {
      hashSmartValue = it->second[0] == "true";
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
  bool writeKey = true;
  it = options.find("--write-key");
  if (it != options.end() && it->second[0] == "false") {
    writeKey = false;
  }
  std::string keyValue;
  it = options.find("--key-value");
  if (it != options.end() && !it->second[0].empty()) {
    keyValue = it->second[0];
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
  int keyValuePos = -1;
  if (type == CSV) {
    // First get the header line:
    if (!getline(vin, line)) {
      std::cerr << "Could not read header line in vertex file " << inputFile
                << std::endl;
      return 3;
    }
    std::vector<std::string> colHeaders = split(line, sep, quo);
    if (colHeaders.size() == 1) {
      std::cerr << "Warning, found only one column in header, did you specify "
                   "the right separator character?"
                << std::endl;
    }
    for (auto &s : colHeaders) {
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
      if (writeKey) {
        keyPos = colHeaders.size();
        colHeaders.push_back("_key");
        newKeyColumn = true;
      }
    }

    if (!keyValue.empty()) {
      keyValuePos = findColPos(colHeaders, keyValue, inputFile);
      if (keyValuePos < 0) {
        if (writeKey) {
          std::cerr << "Warning: could not find column for key value. "
                       "Ignoring..."
                    << std::endl;
        }
      }
    }

    // Write out header:
    bool first = true;
    for (auto const &h : colHeaders) {
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
                         smartValuePos, smartIndex, hashSmartValue, keyPos,
                         keyValuePos, vout);
    } else {
      transformVertexJSONL(line, count, smartAttr, smartValue, smartIndex,
                           hashSmartValue, smartDefault, writeKey, keyValue,
                           vout);
    }

    ++count;

    if (count % 1000000 == 0) {
      std::cout << elapsed() << " Have transformed " << count << " vertices."
                << std::endl;
    }
  }

  vout.close();

  if (!vout.good()) {
    std::cerr << "An error happened at close time for " << outputFile << "."
              << std::endl;
    return 4;
  }
  return 0;
}

void learnSmartKey(Translation &trans, std::string const &key,
                   std::string const &vertexCollName) {
  size_t splitPos = key.find(':');
  if (splitPos != std::string::npos) {
    // Before the colon is the smart graph attribute, after the colon there is
    // the unique key
    std::string uniq = key.substr(splitPos + 1);
    std::string att = key.substr(0, splitPos);
    auto it = trans.attTab.find(att);
    uint32_t pos;
    if (it == trans.attTab.end()) {
      trans.smartAttributes.emplace_back(att);
      pos = static_cast<uint32_t>(trans.smartAttributes.size() - 1);
      trans.attTab.insert(std::make_pair(att, pos));
      trans.memUsage += sizeof(std::pair<std::string, uint32_t>) // attTab
                        + att.size() + 1      // actual string
                        + sizeof(std::string) // smartAttributes
                        + att.size() + 1      // actual string
                        + 32;                 // unordered_map overhead
    } else {
      pos = it->second;
    }
    uniq = vertexCollName + "/" + uniq;
    auto it2 = trans.keyTab.find(uniq);
    if (it2 == trans.keyTab.end()) {
      trans.keyTab.insert(std::make_pair(uniq, pos));
      trans.memUsage += sizeof(std::pair<std::string, uint32_t>) // keyTab
                        + uniq.size() + 1 // actual string
                        + 32;             // unordered_map overhead
    }
  }
}

void learnLineCSV(Translation &trans, std::string const &line, char sep,
                  char quo, int keyPos, std::string const &vertexCollName) {
  std::vector<std::string> parts = split(line, sep, quo);
  std::string key = unquote(parts[keyPos], quo); // Copy here temporarily!
  learnSmartKey(trans, key, vertexCollName);
}

void learnLineJSONL(Translation &trans, std::string const &line,
                    std::string const &vertexCollName) {
  // Parse line to VelocyPack:
  std::shared_ptr<VPackBuilder> b = VPackParser::fromJson(line);
  VPackSlice s = b->slice();
  VPackSlice keySlice = s.get("_key");
  if (!keySlice.isString()) {
    return; // ignore line
  }
  std::string key = keySlice.copyString();
  learnSmartKey(trans, key, vertexCollName);
}

int transformEdgesCSV(std::mutex &mutex, size_t id, Translation &translation,
                      EdgeCollection const &e, char sep, char quo,
                      int smartIndex) {
  {
    std::lock_guard<std::mutex> guard(mutex);
    std::cout << "Transforming edges in " << e.fileName << " ..." << std::endl;
  }
  std::fstream ein(e.fileName, std::ios_base::in);
  std::fstream eout(e.fileName + ".out", std::ios_base::out);
  std::string line;

  // First get the header line:
  if (!getline(ein, line)) {
    {
      std::lock_guard<std::mutex> guard(mutex);
      std::cerr << "Could not read header line in edge file " << e.fileName
                << std::endl;
    }
    return 1;
  }
  std::vector<std::string> colHeaders = split(line, sep, quo);
  if (colHeaders.size() == 1) {
    std::lock_guard<std::mutex> guard(mutex);
    std::cerr << "Warning, found only one column in header, did you specify "
                 "the right separator character?"
              << std::endl;
  }
  for (auto &s : colHeaders) {
    s = unquote(s, quo);
  }
  size_t ncols = colHeaders.size();

  // Rename columns:
  for (auto const &p : e.columnRenames) {
    if (p.first >= 0 && p.first < colHeaders.size()) {
      colHeaders[p.first] = p.second;
    }
  }

  // Write out header:
  bool first = true;
  for (auto const &h : colHeaders) {
    if (!first) {
      eout << sep;
    }
    eout << quote(h, quo);
    first = false;
  }
  eout << "\n";

  // Try to find the _key attribute:
  int keyPos = findColPos(colHeaders, "_key", e.fileName);
  int fromPos = findColPos(colHeaders, "_from", e.fileName);
  int toPos = findColPos(colHeaders, "_to", e.fileName);
  if (fromPos < 0 || toPos < 0) {
    {
      std::lock_guard<std::mutex> guard(mutex);
      std::cerr << id << " Did not find _from or _to field." << std::endl;
    }
    return 2;
  }
  // We tolerate -1 for the key pos, in which case we do not touch it!

  size_t count = 0;

  while (getline(ein, line)) {
    std::vector<std::string> parts = split(line, sep, quo);
    // Extend with empty columns to get at least the right amount of cols:
    while (parts.size() < ncols) {
      parts.emplace_back("");
    }

    auto translate = [&](int pos, std::string const &name,
                         std::string const &vertexCollDefault) -> std::string {
      std::string found = unquote(parts[pos], quo);
      size_t slashpos = found.find('/');
      if (slashpos == std::string::npos) {
        // Prepend the default vertex collection name:
        found = vertexCollDefault + "/" + found;
        parts[pos] = quote(found, quo);
        slashpos = vertexCollDefault.size();
      }
      size_t colPos = found.find(':', slashpos + 1);
      if (colPos != std::string::npos) {
        // already transformed
        return found.substr(slashpos + 1, colPos - slashpos - 1);
      }
      if (smartIndex > 0) {
        // Case of no vertex collections, just prepend a few characters
        // of the key.
        std::string att = found.substr(slashpos + 1, smartIndex);
        parts[pos] = quote(found.substr(0, slashpos + 1) + att + ":" +
                               found.substr(slashpos + 1),
                           quo);
        return att;
      } else {
        auto it = translation.keyTab.find(found);
        if (it == translation.keyTab.end()) {
          // Did not find key, simply go on
          return "";
        }
        std::string key = found.substr(slashpos + 1);
        parts[pos] =
            quote(found.substr(0, slashpos + 1) +
                      translation.smartAttributes[it->second] + ":" + key,
                  quo);
        return translation.smartAttributes[it->second];
      }
    };

    std::string fromAttr = translate(fromPos, "_from", e.fromVertColl);
    std::string toAttr = translate(toPos, "_to", e.toVertColl);

    if (keyPos >= 0 && !fromAttr.empty() && !toAttr.empty()) {
      // See if we have to translate _key as well:
      std::string found = unquote(parts[keyPos], quo);
      size_t colPos1 = found.find(':');
      if (colPos1 == std::string::npos) {
        // both positions found, need to add both attributes:
        parts[keyPos] = quote(fromAttr + ":" + found + ":" + toAttr, quo);
      }
    }

    // Write out the potentially modified line:
    eout << parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
      eout << sep << parts[i];
    }
    eout << '\n';

    ++count;

    if (count % 1000000 == 0) {
      std::lock_guard<std::mutex> guard(mutex);
      std::cout << id << " " << elapsed() << " Have transformed " << count
                << " edges in " << e.fileName << "..." << std::endl;
    }
  }

  {
    std::lock_guard<std::mutex> guard(mutex);
    std::cout << id << " " << elapsed() << " Have transformed " << count
              << " edges in " << e.fileName << ", finished." << std::endl;
  }

  ein.close();
  eout.close();

  if (!eout.good()) {
    std::cerr << "An error happened at close time for " << e.fileName + ".out"
              << ", not renaming to the original name." << std::endl;
    return 4;
  }

  ::unlink(e.fileName.c_str());
  ::rename((e.fileName + ".out").c_str(), e.fileName.c_str());
  return 0;
}

int transformEdgesJSONL(std::mutex &mutex, size_t id, Translation &translation,
                        EdgeCollection const &e, int smartIndex) {
  {
    std::lock_guard<std::mutex> guard(mutex);
    std::cout << id << " " << elapsed() << " Transforming edges in "
              << e.fileName << " ..." << std::endl;
  }
  std::fstream ein(e.fileName, std::ios_base::in);
  std::fstream eout(e.fileName + ".out", std::ios_base::out);
  std::string line;

  size_t count = 0;

  while (getline(ein, line)) {
    // Parse line to VelocyPack:
    std::shared_ptr<VPackBuilder> b = VPackParser::fromJson(line);
    VPackSlice s = b->slice();

    auto translate =
        [&](std::string const &name, std::string const &vertexCollDefault,
            std::string &newValue, bool &foundFlag) -> std::string {
      VPackSlice foundSlice = s.get(name);
      if (!foundSlice.isString()) {
        {
          std::lock_guard<std::mutex> guard(mutex);
          std::cerr << id << " Found " << name
                    << " entry which is not a string:\n"
                    << line << std::endl;
        }
        foundFlag = false;
        return "";
      }
      foundFlag = true;
      newValue = foundSlice.copyString();
      size_t slashpos = newValue.find('/');
      if (slashpos == std::string::npos) {
        // Prepend the default vertex collection name:
        newValue = vertexCollDefault + "/" + newValue;
        slashpos = vertexCollDefault.size();
      }
      size_t colPos = newValue.find(':', slashpos + 1);
      if (colPos != std::string::npos) {
        // already transformed
        return newValue.substr(slashpos + 1, colPos - slashpos - 1);
      }
      if (smartIndex > 0) {
        // Case of no vertex collections, just prepend a few characters
        // of the key.
        std::string att = newValue.substr(slashpos + 1, smartIndex);
        newValue = newValue.substr(0, slashpos + 1) + att + ":" +
                   newValue.substr(slashpos + 1);
        return att;

      } else {
        auto it = translation.keyTab.find(newValue);
        if (it == translation.keyTab.end()) {
          // Did not find key, simply go on
          return "";
        }
        std::string key = newValue.substr(slashpos + 1);
        newValue = newValue.substr(0, slashpos + 1) +
                   translation.smartAttributes[it->second] + ":" + key;
        return translation.smartAttributes[it->second];
      }
    };

    bool foundFrom;
    std::string newFrom;
    std::string fromAttr =
        translate("_from", e.fromVertColl, newFrom, foundFrom);
    bool foundTo;
    std::string newTo;
    std::string toAttr = translate("_to", e.toVertColl, newTo, foundTo);

    std::string newKey;
    bool foundKey = false;
    if (!fromAttr.empty() && !toAttr.empty()) {
      // See if we have to translate _key as well:
      VPackSlice keySlice = s.get("_key");
      if (keySlice.isString()) {
        foundKey = true;
        std::string found = keySlice.copyString();
        size_t colPos1 = found.find(':');
        if (colPos1 == std::string::npos) {
          // both positions found, need to add both attributes:
          newKey = fromAttr + ":" + found + ":" + toAttr;
        }
      }
    }

    // Write out the potentially modified line:
    bool written = false;
    auto output = [&](bool found, std::string const &name,
                      std::string const &newVal) {
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

    eout << '{';
    output(foundKey, "_key", newKey);
    output(foundFrom, "_from", newFrom);
    output(foundTo, "_to", newTo);

    for (auto const &p : VPackObjectIterator(s)) {
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
      std::lock_guard<std::mutex> guard(mutex);
      std::cout << id << " " << elapsed() << " Have transformed " << count
                << " edges in " << e.fileName << "..." << std::endl;
    }
  }

  {
    std::lock_guard<std::mutex> guard(mutex);
    std::cout << id << " " << elapsed() << " Have transformed " << count
              << " edges in " << e.fileName << ", finished." << std::endl;
  }

  ein.close();
  eout.close();

  if (!eout.good()) {
    std::lock_guard<std::mutex> guard(mutex);
    std::cerr << id << " An error happened at close time for "
              << e.fileName + ".out" << ", not renaming to the original name."
              << std::endl;
    return 1;
  }

  ::unlink(e.fileName.c_str());
  ::rename((e.fileName + ".out").c_str(), e.fileName.c_str());
  return 0;
}

struct VertexBuffer {
public:
  std::vector<std::string> _vertexCollNames;
  std::vector<std::string> _vertexFiles;

private:
  Translation _trans;
  size_t _filePos;
  std::ifstream _currentInput;
  bool _fileOpen;
  DataType _type;
  int _keyPos;
  char _separator;
  char _quoteChar;
  uint64_t _count;

public:
  VertexBuffer(DataType type, char separator, char quoteChar)
      : _filePos(0), _fileOpen(false), _type(type), _keyPos(0),
        _separator(separator), _quoteChar(quoteChar), _count(0) {}

  bool isDone() { return _filePos >= _vertexFiles.size(); }

  // Note that an empty VertexBuffer will be `isDone` right from the beginning,
  // however, it is still possible to call `readMore` once. This is used in the
  // case of the edge transformation without vertex collections.

  int readMore(size_t memLimit) {
    std::cout << elapsed() << " Reading vertices..." << std::endl;
    std::string line;
    _trans.clear();
    while (_filePos < _vertexFiles.size()) {
      if (_trans.memUsage >= memLimit) {
        break;
      }
      if (!_fileOpen) {
        std::cout << elapsed() << " Opening vertex file "
                  << _vertexFiles[_filePos] << " ..." << std::endl;
        _currentInput.open(_vertexFiles[_filePos].c_str(), std::ios::in);
        _count = 0;
        if (_currentInput.good()) {
          _fileOpen = true;
        } else {
          std::cerr << "Could not open file " << _vertexFiles[_filePos]
                    << " for reading." << std::endl;
          return 1;
        }
        if (_type == CSV) {
          // Read header:
          if (!getline(_currentInput, line)) {
            std::cerr << "Could not read header line in vertex file "
                      << _vertexFiles[_filePos] << ", giving up." << std::endl;
            return 2;
          }
          std::vector<std::string> colHeaders =
              split(line, _separator, _quoteChar);
          for (auto &s : colHeaders) {
            s = unquote(s, _quoteChar);
          }

          _keyPos = findColPos(colHeaders, "_key", _vertexFiles[_filePos]);
          if (_keyPos < 0) {
            return 3;
          }
        } else {
          // JSONL case
          // ...
        }
      }
      if (!getline(_currentInput, line)) {
        _currentInput.close();
        ++_filePos;
        _fileOpen = false;
        continue; // will read more from next file
      }
      ++_count;
      if (_type == CSV) {
        learnLineCSV(_trans, line, _separator, _quoteChar, _keyPos,
                     _vertexCollNames[_filePos]);
      } else {
        learnLineJSONL(_trans, line, _vertexCollNames[_filePos]);
      }
      if (_count % 1000000 == 0) {
        std::cout << elapsed() << " Have read " << _count << " vertices (needs "
                  << _trans.memUsage / (1024 * 1024) << " MB of RAM)."
                  << std::endl;
      }
    }
    std::cout << elapsed() << " Have read " << _trans.memUsage / (1024 * 1024)
              << " MB of vertex data." << std::endl;
    return 0;
  }

  Translation &translation() { return _trans; }
};

int doEdges(Options const &options) {
  // Check options, find vertex colls and edge colls
  DataType type = CSV;
  auto it = options.find("--type");
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

  it = options.find("--memory");
  assert(it != options.end()); // there is a default
  size_t memLimit =
      strtoul(it->second[0].c_str(), nullptr, 10) * 1024 * 1024; // in MBs
                                                                 //
  int64_t smartIndex = -1; // does not count
  it = options.find("--smart-index");
  if (it != options.end()) {
    smartIndex = strtol(it->second[0].c_str(), nullptr, 10);
  }
  size_t nrThreads = 1;
  it = options.find("--threads");
  if (it != options.end()) {
    nrThreads = strtoul(it->second[0].c_str(), nullptr, 10);
  }

  // Set up translator and set up vertex reader object
  // while vertex reader object not done
  //   run through all edge collections, one at a time
  //     transform what can be done, write to tmp file
  //     move tmp file to original file
  //   forget all vertex data
  //   read more vertex data
  VertexBuffer vertexBuffer(type, sep, quo);

  // Add vertex collections:
  it = options.find("--vertices");
  if (it == options.end()) {
    // Strange, no vertex collections, there is only one use case, namely,
    // that `--smart-value` is `_key` (implicit) and `--smart_index` is
    // set. Then the smart graph attribute is a prefix of the key and
    // thus can be derived from the key without lookup, therefore we
    // need no vertex collections. Let's check this:
    if (smartIndex <= 0) {
      std::cerr << "Need at least one vertex collection with the `--vertices` "
                   "option. Giving up."
                << std::endl;
      return 1;
    }
  } else {
    for (auto const &s : it->second) {
      auto pos = s.find(":");
      if (pos == std::string::npos) {
        std::cerr << "Value for `--vertices` option needs to be of the form "
                     "<collname>:<collfile>, but is: "
                  << s << " Giving up." << std::endl;
        return 2;
      }
      vertexBuffer._vertexCollNames.push_back(s.substr(0, pos));
      vertexBuffer._vertexFiles.push_back(s.substr(pos + 1));
    }
  }

  // Get the edge collections data:
  std::vector<EdgeCollection> edgeCollections;
  it = options.find("--edges");
  if (it == options.end()) {
    std::cerr << "Need at least one edge collection with the `--edges` "
                 "option. Giving up."
              << std::endl;
    return 3;
  }
  for (auto const &e : it->second) {
    auto pos = e.find(':');
    if (pos == std::string::npos) {
      std::cerr << "Value for `--edges` option needs to be of the form "
                   "<edgefilename>:<vertcollname>:<vertcollname>, but is: "
                << e << " Giving up." << std::endl;
      return 4;
    }
    auto pos2 = e.find(':', pos + 1);
    if (pos2 == std::string::npos) {
      std::cerr << "Value for `--edges` option needs to be of the form "
                   "<edgefilename>:<vertcollname>:<vertcollname>, but is: "
                << e << " Giving up." << std::endl;
      return 5;
    }
    auto pos3 = e.find(':', pos2 + 1);
    std::vector<std::pair<int, std::string>> renames;
    if (pos3 != std::string::npos) {
      // Need to read column renames:
      std::string renamest = e.substr(pos3 + 1);
      auto parts = split(renamest, ':', '"');
      for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        int nr = strtoul(parts[i].c_str(), nullptr, 10);
        renames.emplace_back(std::make_pair(nr, parts[i + 1]));
      }
    } else {
      pos3 = e.size();
    }
    edgeCollections.push_back(
        EdgeCollection{.fileName = e.substr(0, pos),
                       .fromVertColl = e.substr(pos + 1, pos2 - pos - 1),
                       .toVertColl = e.substr(pos2 + 1, pos3 - pos2 - 1),
                       .columnRenames = std::move(renames)});
  }

  // Main work:
  do {
    vertexBuffer.readMore(memLimit);
    std::deque<EdgeCollection> queue;
    std::mutex mutex;
    int error = 0;
    for (auto const &e : edgeCollections) {
      queue.push_back(e);
    }
    auto worker = [&](size_t id) {
      while (true) { // left when queue empty
        EdgeCollection e;
        {
          std::lock_guard<std::mutex> guard(mutex);
          if (queue.size() == 0) {
            break;
          }
          e = queue[0];
          queue.pop_front();
        }
        if (type == CSV) {
          if (transformEdgesCSV(mutex, id, vertexBuffer.translation(), e, sep,
                                quo, smartIndex) != 0) {
            error = 6;
          }
        } else {
          if (transformEdgesJSONL(mutex, id, vertexBuffer.translation(), e,
                                  smartIndex) != 0) {
            error = 7;
          }
        }
      }
    };
    std::vector<std::thread> threads;
    for (size_t i = 0; i < nrThreads; ++i) {
      threads.emplace_back(worker, i);
    }
    for (size_t i = 0; i < nrThreads; ++i) {
      threads[i].join();
    }
    if (error != 0) {
      return error;
    }
  } while (!vertexBuffer.isDone());
  return 0;
}

#define MYASSERT(t)                                                            \
  if (!(t)) {                                                                  \
    std::cerr << "Error in line " << __LINE__ << std::endl;                    \
  }

void runTests() {
  std::string s = quote("abc", '"');
  MYASSERT(s == "abc");
  s = quote("a\"b\"c", '"');
  MYASSERT(s == "\"a\"\"b\"\"c\"");
  s = unquote("\"xyz\"", '"');
  MYASSERT(s == "xyz");
  s = unquote("xyz", '"');
  MYASSERT(s == "xyz");
  s = unquote("\"xy\"\"z\"", '"');
  MYASSERT(s == "xy\"z");
  s = quote("abc", 'a');
  MYASSERT(s == "aaabca");

  auto v = split("a,b,c", ',', '"');
  MYASSERT(v.size() == 3);
  MYASSERT(v[0] == "a");
  MYASSERT(v[1] == "b");
  MYASSERT(v[2] == "c");

  v = split("\"a,b\",c", ',', '"');
  MYASSERT(v.size() == 2);
  MYASSERT(v[0] == "\"a,b\"");
  MYASSERT(v[1] == "c");

  v = split("\"a,b\",c", ',', '"');
  MYASSERT(v.size() == 2);
  MYASSERT(unquote(v[0], '"') == "a,b");
  MYASSERT(v[1] == "c");

  v = split("\"a,\"\"b\",c", ',', '"');
  MYASSERT(v.size() == 2);
  MYASSERT(v[0] == "\"a,\"\"b\"");
  MYASSERT(v[1] == "c");

  v = split("\"a,\"\"b\",c", ',', '"');
  MYASSERT(v.size() == 2);
  MYASSERT(unquote(v[0], '"') == "a,\"b");
  MYASSERT(v[1] == "c");

  v = split("\"a\"x\"a\",b,c", ',', '"');
  MYASSERT(v.size() == 3);
  MYASSERT(unquote(v[0], '"') == "aa");
  MYASSERT(v[1] == "b");
  MYASSERT(v[2] == "c");
}

int main(int argc, char *argv[]) {
  startTime = std::chrono::steady_clock::now();
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
      {"--hash-smart-value", OptionConfigItem(ArgType::Bool, "false")},
      {"--from-attribute", OptionConfigItem(ArgType::StringOnce, "_from")},
      {"--to-attribute", OptionConfigItem(ArgType::StringOnce, "_to")},
      {"--vertices", OptionConfigItem(ArgType::StringMultiple)},
      {"--edges", OptionConfigItem(ArgType::StringMultiple)},
      {"--rename-column", OptionConfigItem(ArgType::StringMultiple)},
      {"--smart-default", OptionConfigItem(ArgType::StringOnce)},
      {"--threads", OptionConfigItem(ArgType::StringOnce, "1")},
      {"--key-value", OptionConfigItem(ArgType::StringOnce)},
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
                 "." GRAPHUTILS_VERSION_MINOR; // version string
    return 0;
  }
  it = options.find("--test");
  if (it != options.end() && it->second[0] == "true") {
    std::cout << "Running unit tests...\n";
    runTests();
    std::cout << "Done." << std::endl;
    return 0;
  }
  it = options.find("--randomize-smart");
  if (it != options.end() && it->second[0] != "false") {
    std::cout << "--randomize-smart is not yet implemented, giving up."
              << std::endl;
    return 1;
  }

  if (args.size() != 1 || (args[0] != "vertices" && args[0] != "edges")) {
    std::cerr << "Need exactly one subcommand 'vertices' or 'edges'.\n";
    return -2;
  }

  if (args[0] == "vertices") {
    return doVertices(options);
  } else if (args[0] == "edges") {
    return doEdges(options);
  }

  return 0;
}
