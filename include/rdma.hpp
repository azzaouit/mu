#ifndef RDMA_HPP
#define RDMA_HPP

#include <infiniband/verbs.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <config.hpp>

typedef std::pair<void *, size_t> LocalMR;
typedef std::pair<struct ibv_mr *, struct ibv_qp *> RemoteMR;

struct ConnectionAttr {
  uint64_t addr;
  uint32_t rkey;
  uint16_t lid;
  uint32_t qpn;
  uint32_t psn;
  uint8_t gid[16];
} __attribute__((packed));

class Rdma {
private:
  uint16_t lid;
  union ibv_gid gid;
  struct ibv_pd *pd;
  struct ibv_context *ctx;
  struct ibv_port_attr portinfo;
  std::vector<struct ibv_cq *> cq;
  std::map<std::string, std::vector<RemoteMR>> mr;
  std::map<std::string, std::vector<ConnectionAttr>> attr;
  int add_remote_mr(PeerConfig &p, std::vector<LocalMR> &lmr, bool leader,
                    int port);

public:
  Rdma() = default;
  int open(NetConfig &n, std::vector<LocalMR> &lmr, int leader);
  ~Rdma();
};

#endif /* RDMA_HPP */
