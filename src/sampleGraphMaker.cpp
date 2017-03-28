// This program creates a social-network like graph as CSV data

#include "GraphUtilsConfig.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <random>

#include "velocypack/Slice.h"
#include "velocypack/Builder.h"
#include "velocypack/Parser.h"
#include "velocypack/velocypack-aliases.h"

#include "docopt.h"

static const char USAGE[] =
R"(SampleGraphMaker - make a sample social graph of configurable size

    Usage:
      sampleGraphMaker [--type=<type>] 
               <baseName> <numberVertices> <numberEdges> [<seed>]

    Options:
      -h --help                Show this screen.
      --version                Show version.
      --type=<type>            Data type "csv" or "jsonl" [default: csv].
      <baseName>               Name prefix for files.
      <numberVertices>         Number of vertices.
      <edgeFile>               File for the edges.
      <seed>                   Smart graph attribute [default: 1].
)";

enum DataType {
  CSV = 0,
  JSONL = 1
};

std::vector<std::string> cities = {"San Francisco", "New York", "Eppelheim"};
std::vector<std::string> streets = {"Main Street", "Baker Street", "Butcher Street"};
std::vector<std::string> emails = {"miller", "meier", "hans", "karl"};
std::vector<std::string> countries = {"DE", "US", "FR", "UK", "AU", "CA", "MX"};

int main(int argc, char* argv[]) {
  std::map<std::string, docopt::value> args
      = docopt::docopt(USAGE,
		       { argv + 1, argv + argc },
		       true,               // show help if requested
		       "sampleGraphMaker V"
                       GRAPHUTILS_VERSION_MAJOR "."
                       GRAPHUTILS_VERSION_MINOR);  // version string
  DataType type = CSV;
  if (args["--type"].asString() == "jsonl") {
    type = JSONL;
  }
  std::string name = args["<baseName>"].asString();
  std::string vname = name + "_profiles."
                    + (type == CSV ? "csv" : "jsonl");
  std::string ename = name + "_relations."
                    + (type == CSV ? "csv" : "jsonl");
  long nrVert = args["<numberVertices>"].asLong();
  long nrEdge = args["<numberEdges>"].asLong();
  long seed = args["<seed>"].asLong();

  // Random:
  std::mt19937_64 random;
  random.seed(seed);

  std::fstream outv(vname, std::ios_base::out);

  if (type == CSV) {
    outv << "_key,name,keybak,country,telephone,email,age,gender,address\n";
    for (long i = 1; i <= nrVert; ++i) {
      outv << '"' << i << "\",name" << i << "," << i << ","
           << countries[random() % countries.size()] << ",\""
           << 1518384838843 + i << "\"," << emails[random() % emails.size()]
           << "@person" << i << ".com,";
      auto age = (random() % 80) + 20;
      auto gender = random() % 2;
      auto zip = random() % 100000 + 1;
      outv << age << "," << (gender == 0 ? "M," : "F,") << (random() % 100) + 1
           << " " << streets[random() % streets.size()] << ";"
           << cities[random() % cities.size()]
           << ";" << zip << "\n";
      if (i % 1000000 == 0) {
        std::cout << "Have written " << i << " vertices out of " << nrVert
          << " ..." << std::endl;
      }
    }
  } else {  // JSONL
    outv << "_key,name,keybak,country,telephone,email,age,gender,address\n";
    for (long i = 1; i <= nrVert; ++i) {
      outv << '{'
           << R"("_key":")" << i << "\","
           << R"("name":"name)" << i << "\","
           << R"("keybak":)" << i << ","
           << R"("country":")" << countries[random()%countries.size()] << "\","
           << R"("telephone":")" << 1518384838843 + i << "\"," 
           << R"("email":")" << emails[random() % emails.size()]
           << "@person" << i << ".com\",";
      auto age = (random() % 80) + 20;
      auto gender = random() % 2;
      auto zip = random() % 100000 + 1;
      outv << R"("age":)" << age << "," 
           << R"("gender":")" << (gender == 0 ? "M\"," : "F\",") 
           << R"("address":")" << (random() % 100) + 1
           << " " << streets[random() % streets.size()] << ";"
           << cities[random() % cities.size()]
           << ";" << zip << "\"}\n";
      if (i % 1000000 == 0) {
        std::cout << "Have written " << i << " vertices out of " << nrVert
          << " ..." << std::endl;
      }
    }
  }

  std::fstream oute(ename, std::ios_base::out);

  for (long i = 1; i <= nrEdge; ++i) {
    if (type == CSV) {
      oute << "_key,_from,_to\n";
        auto from = random() % nrVert + 1;
        auto to = random() % nrVert + 1;
        oute << '"' << i << "\",profiles/" << from << ",profiles/" << to << "\n";
    } else {  // JSONL
      auto from = random() % nrVert + 1;
      auto to = random() % nrVert + 1;
      oute << '{'
           << R"("_key":")" << i << "\","
           << R"("_from":"profiles/)" << from << "\","
           << R"("_to":"profiles/)" << to << "\"}\n";
    }
    if (i % 1000000 == 0) {
      std::cout << "Have written " << i << " edges out of " << nrEdge
        << " ..." << std::endl;
    }
  }

  std::cout << "\nYou might want to import the graph using the following:\n\n"
    << "  arangoimp --collection profiles --file " << vname
    << " --type " << (type == CSV ? "csv" : "json")
    << "\\\n        --separator ,\n"
    << "  arangoimp --collection relations --file " << ename
    << " --type " << (type == CSV ? "csv" : "json")
    << "\\\n        --separator ,\n\n"
    << "but first create a vertex collection 'profiles' and an edge "
       "collection\n'relations'. Use '--server.endpoint' to point "
       "arangoimp to your DB endpoint." << std::endl;
  return 0;
}
