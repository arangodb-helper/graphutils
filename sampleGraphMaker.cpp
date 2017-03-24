// This program creates a social-network like graph as CSV data

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <random>

std::vector<std::string> cities = {"San Francisco", "New York", "Eppelheim"};
std::vector<std::string> streets = {"Main Street", "Baker Street", "Butcher Street"};
std::vector<std::string> emails = {"miller", "meier", "hans", "karl"};
std::vector<std::string> countries = {"DE", "US", "FR", "UK", "AU", "CA", "MX"};

int main(int argc, char* argv[]) {
  if (argc < 5) {
    std::cout << "Usage: sampleGraphMaker <NAME> <NRVERTS> <NREDGES> <SEED>"
      << std::endl;
    return 0;
  }
  std::string name = argv[1];
  std::string vname = name + "_profiles.csv";
  std::string ename = name + "_relations.csv";
  long nrVert = std::stol(std::string(argv[2]));
  long nrEdge = std::stol(std::string(argv[3]));
  long seed = std::stol(std::string(argv[4]));

  // Random:
  std::mt19937_64 random;
  random.seed(seed);

  std::fstream outv(vname, std::ios_base::out);

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
        << std::endl;
    }
  }

  std::fstream oute(ename, std::ios_base::out);

  oute << "_key,_from,_to\n";
  for (long i = 1; i <= nrEdge; ++i) {
    auto from = random() % nrVert + 1;
    auto to = random() % nrVert + 1;
    oute << '"' << i << "\",profiles/" << from << ",profiles/" << to << "\n";

    if (i % 1000000 == 0) {
      std::cout << "Have written " << i << " edges out of " << nrEdge
        << std::endl;
    }
  }

  std::cout << "\nYou might want to import the graph using the following:\n\n"
    << "  arangoimp --collection profiles --file " << name
    << "_profiles.csv --type csv \\\n        --separator ,\n"
    << "  arangoimp --collection relations --file " << name
    << "_relations.csv --type csv \\\n        --separator ,\n\n"
    << "but first create a vertex collection 'profiles' and an edge "
       "collection\n'relations'. Use '--server.endpoint' to point "
       "arangoimp to your DB endpoint." << std::endl;
  return 0;
}
