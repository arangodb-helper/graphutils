// This tool allows to transfer CSV data of a graph into smart graph format

#include "GraphUtilsConfig.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <unistd.h>

#include "velocypack/Slice.h"
#include "velocypack/Builder.h"
#include "velocypack/Parser.h"
#include "velocypack/velocypack-aliases.h"

#include "docopt.h"

static const char USAGE[] =
R"(Smartifier - transform graph data into smart graph format

    Usage:
      smartifier [--type=<type>] [--separator=<separator>]
                 [--quoteChar=<quoteChar>] [--memory=MEMORY]
                 <vertexFile> <vertexColl> <edgeFile> <smartGraphAttr>

    Options:
      -h --help                Show this screen.
      --version                Show version.
      --type=<type>            Data type "csv" or "jsonl" [default: csv]
      --separator=<separator>  Column separator for csv type [default: ,]
      --quoteChar=<quoteChar>  Quote character for csv type [default: "]
      --memory=<memory>        Limit RAM usage in MiB [default: 4096]
      <vertexFile>             File for the vertices.
      <vertexColl>             Name of vertex collection.
      <edgeFile>               File for the edges.
      <smartGraphAttr>         Smart graph attribute.
)";

VPackBuilder parseLine(std::string const& line) {
  VPackBuilder b;
  b.add(VPackValue("Hallo"));
  return b;
}

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
        if (pos+1 < line.size() && line[pos+1] == quo) {
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
                  std::string const& header,
                  std::string const& fileName) {
  auto it = std::find(colHeaders.begin(), colHeaders.end(), header);
  if (it == colHeaders.end()) {
    std::cerr << "Did not find " << header << " in column headers in file '"
      << fileName << "'" << std::endl;
    return -1;
  }
  return static_cast<int>(it - colHeaders.begin());
}

void transformEdges(std::unordered_map<std::string, uint32_t> const& keyTab,
                    std::vector<std::string> const& attTab,
                    std::string const& vcolname,
                    std::string const& ename,
                    char sep, char quo) {
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
      if (found.size() > 1 && found[0] == quo && found[found.size()-1] == quo) {
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
      auto it = keyTab.find(key);
      if (it == keyTab.end()) {
        // Did not find key, simply go on
        return "";
      }
      if (quoted) {
        parts[pos] = quo + found.substr(0, slashpos + 1) + attTab[it->second]
                         + ":" + key + quo;
      } else {
        parts[pos] = found.substr(0, slashpos + 1) + attTab[it->second]
                   + ":" + key;
      }
      return attTab[it->second];
    };
    
    std::string fromAttr = translate(fromPos, "_from");
    std::string toAttr = translate(toPos, "_to");

    if (keyPos >= 0 && !fromAttr.empty() && !toAttr.empty()) {
      // See if we have to translate _key as well:
      std::string found = parts[keyPos];
      bool quoted = false;
      if (found.size() > 1 && found[0] == quo && found[found.size()-1] == quo) {
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

int main(int argc, char* argv[]) {
  std::map<std::string, docopt::value> args
      = docopt::docopt(USAGE,
		       { argv + 1, argv + argc },
		       true,               // show help if requested
		       "smartifier V"
                       GRAPHUTILS_VERSION_MAJOR "."
                       GRAPHUTILS_VERSION_MINOR);  // version string

  for (auto const& p : args) {
    std::cout << "Key: " << p.first << " Value: " << p.second << std::endl;
  }
  std::string vname = args["<vertexFile>"].asString();
  std::string vcolname = args["<vertexColl>"].asString();
  std::string ename = args["<edgeFile>"].asString();
  std::string smartAttr = args["<smartGraphAttr>"].asString();
  size_t memMB= args["--memory"].asLong();
  char sep = args["--separator"].asString()[0];
  char quo = args["--quoteChar"].asString()[0];

  std::fstream vin(vname, std::ios_base::in);
  std::string line;

  // First get the header line:
  if (!getline(vin, line)) {
    std::cerr << "Could not read header line in vertex file " << vname
      << std::endl;
    return 1;
  }
  std::vector<std::string> colHeaders = split(line, sep, quo);
  size_t ncols = colHeaders.size();

  int smartAttrPos = findColPos(colHeaders, smartAttr, vname);
  if (smartAttrPos < 0) {
    return 2;
  }

  int keyPos = findColPos(colHeaders, "_key", vname);
  if (keyPos < 0) {
    return 3;
  }
  
  // Prepare output file for vertices:
  std::fstream vout(vname + ".out", std::ios_base::out);
  
  // Write out header:
  vout << line << "\n";

  bool done = false;
  size_t count = 0;
  while (!done) {
    // We do one batch of vertices in one run of this loop
    std::unordered_map<std::string, uint32_t> keyTab;
    std::unordered_map<std::string, uint32_t> attTab;
    std::vector<std::string> smartAttributes;
    size_t memUsage = 0;   // strings in map plus table size
    while (!done && memUsage < memMB*1024*1024) {
      if (!getline(vin, line)) {
        done = true;
        break;
      }
      std::vector<std::string> parts = split(line, sep, quo);
      // Extend with empty columns to get at least the right amount of cols:
      while (parts.size() < ncols) {
        parts.emplace_back("");
      }
      
      // Store smartGraphAttribute in infrastructure if not already seen:
      std::string const& att = parts[smartAttrPos];
      auto it = attTab.find(att);
      uint32_t pos;
      if (it == attTab.end()) {
        smartAttributes.emplace_back(att);
        pos = static_cast<uint32_t>(smartAttributes.size() - 1);
        attTab.insert(std::make_pair(att, pos));
        memUsage += sizeof(std::pair<std::string, uint32_t>) // attTab
                    + att.size()+1                           // actual string
                    + sizeof(std::string)                    // smartAttributes
                    + att.size()+1;                          // actual string
      } else {
        pos = it->second;
      }

      // Put the smart graph attribute into a prefix of the key, if it
      // is not already there:
      std::string key = parts[keyPos];  // Copy here temporarily!
      bool quoted = false;
      if (key.size() > 1 && key[0] == quo && key[key.size()-1] == quo) {
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
      it = keyTab.find(key);
      if (it == keyTab.end()) {
        keyTab.insert(std::make_pair(key, pos));
        memUsage += sizeof(std::pair<std::string, uint32_t>)   // keyTab
                    + key.size()+1;                            // actual string
      }

      // Write out the potentially modified line:
      vout << parts[0];
      for (size_t i = 1; i < parts.size(); ++i) {
        vout << ',' << parts[i];
      }
      vout << '\n';

      ++count;

      if (count % 1000000 == 0) {
        std::cout << "Have transformed " << count << " vertices, memory: "
          << memUsage / (1024*1024) << " MB ..." << std::endl;
      }
    }
    if (count % 1000000 != 0) {
      std::cout << "Have transformed " << count << " vertices, memory: "
        << memUsage / (1024*1024) << " MB ..." << std::endl;
    }
    transformEdges(keyTab, smartAttributes, vcolname, ename, sep, quo);
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
