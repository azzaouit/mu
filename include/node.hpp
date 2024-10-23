#ifndef NODE_H
#define NODE_H

#include <map>

#include <config.hpp>
#include <rdma.hpp>

enum MU_PLANE {
  MU_REP = 0,
  MU_BG,
};

struct Slot {
  uint32_t propno;
  int32_t value;
} __attribute__((packed));

/* Replication log invariant: only one replica has write access. */
struct Log {
  int min_proposal;
  int FUO;
  int slot;
  Slot slots[64];
};

struct Background {
  uint64_t heart;
  std::map<std::string, uint8_t> perms;
};

class Node {
private:
  uint16_t leader;
  NetConfig &c;
  Background bg;
  Log log;
  Rdma r;

public:
  Node(NetConfig &c);
  int rdma_init();
  ~Node() = default;
};

#endif /* NODE_H */
