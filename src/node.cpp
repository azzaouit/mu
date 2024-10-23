#include <node.hpp>
#include <string>

Node::Node(NetConfig &c) : c(c) {
  leader = c.host.config.id;
  for (auto &p : c.peers)
    if (p.config.id < leader)
      leader = p.config.id;
};

int Node::rdma_init() {
  std::vector<LocalMR> lmr;
  lmr.push_back(std::make_pair((void *)&log, sizeof log));
  lmr.push_back(std::make_pair((void *)&bg, sizeof bg));
  return r.open(c, lmr, leader);
}
