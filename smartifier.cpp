// This tool allows to transfer CSV data of a graph into smart graph format

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>

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

void transformEdges(std::unordered_map<std::string, uint32_t> const& keyTab,
                    std::vector<std::string> const& attTab,
                    std::string ename) {
  std::cout << "Transforming edges in " << ename << std::endl;
  size_t count = 0;
  for (auto const& s : attTab) {
    std::cout << "pos: " << count++ << " val: " << s << "\n";
  }
  for (auto const& p : keyTab) {
    std::cout << "Key: " << p.first << " Value: " << p.second << "\n";
  }
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    std::cerr << "Usage: smartifier <VERTEXFILE> <EDGEFILE> <SMARTGRAPHATTR> "
      "<MEMSIZE_IN_GB> [<SEPARATOR> [<QUOTECHAR>]]" << std::endl;
    return 0;
  }
  std::string vname = argv[1];
  std::string ename = argv[2];
  std::string smartAttr = argv[3];
  size_t memGB= std::stoul(argv[4]);
  char sep = ',';
  char quo = '"';
  if (argc >= 6) {
    sep = argv[5][0];
    if (argc >= 7) {
      quo = argv[6][0];
    }
  }

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

  // Try to find the smart graph attribute:
  auto it = std::find(colHeaders.begin(), colHeaders.end(), smartAttr);
  if (it == colHeaders.end()) {
    std::cerr << "Did not find smartAttr " << smartAttr << " in column headers."
      << std::endl;
    return 2;
  }
  size_t smartAttrPos = it - colHeaders.begin();

  // Try to find the _key attribute:
  it = std::find(colHeaders.begin(), colHeaders.end(), std::string("_key"));
  if (it == colHeaders.end()) {
    std::cerr << "Did not find _key in column headers."
      << std::endl;
    return 3;
  }
  size_t keyPos = it - colHeaders.begin();
  
  // Prepare output file for vertices:
  std::fstream vout(vname + "_out", std::ios_base::out);
  
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
    while (!done && memUsage < memGB*1024*1024*1024) {
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
        std::cout << "Have transformed " << count << " vertices." << std::endl;
      }
    }
    transformEdges(keyTab, smartAttributes, ename);
  }

  return 0;
}
