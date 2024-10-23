#include <fstream>
#include <iostream>

#include <node.hpp>

int main() {
  NetConfig cfg;
  std::ifstream ifile("/etc/murc");
  ifile >> cfg;

  std::cout << cfg;

  Node n(cfg);
  n.rdma_init();

  return 0;
}
