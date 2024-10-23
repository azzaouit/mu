#include <assert.h>
#include <utility>
#include <vector>

#include <config.hpp>
#include <rdma.hpp>

int Rdma::open(NetConfig &n, std::vector<LocalMR> &lmr, int leader) {
  size_t i;
  struct ibv_cq *q = nullptr;
  struct ibv_device **dev_list = nullptr;

  if (!(dev_list = ibv_get_device_list(NULL))) {
    MU_LOG_ERR("ibv_get_device_list failed");
    return -errno;
  }

  if (!(ctx = ibv_open_device(dev_list[0]))) {
    MU_LOG_ERR("ibv_open_device failed");
    ibv_free_device_list(dev_list);
    return -errno;
  }
  ibv_free_device_list(dev_list);

  if (ibv_query_gid(ctx, n.host.config.ib_port, n.host.config.gid_index,
                    &gid)) {
    MU_LOG_ERR("ibv_query_gid failed");
    return -errno;
  }

  if (ibv_query_port(ctx, n.host.config.ib_port, &portinfo)) {
    MU_LOG_ERR("ibv_query_port failed");
    return -errno;
  }

  if (!(pd = ibv_alloc_pd(ctx))) {
    MU_LOG_ERR("ibv_alloc_pd failed");
    return -errno;
  }

  for (i = 0; i < lmr.size(); ++i) {
    if (!(q = ibv_create_cq(ctx, MU_CQ_CAPACITY, NULL, NULL, 0))) {
      MU_LOG_ERR("ibv_create_cq failed");
      goto err;
    }
    cq.push_back(q);
  }

  for (auto &p : n.peers)
    if (add_remote_mr(p, lmr, p.config.id == leader, n.host.config.ib_port))
      goto errmr;

  return 0;

errmr:
  for (auto &m : mr)
    for (auto &r : m.second) {
      ibv_dereg_mr(r.first);
      ibv_destroy_qp(r.second);
    }
err:
  for (size_t j = 0; j < i; ++j)
    ibv_destroy_cq(cq[i]);
  ibv_dealloc_pd(pd);
  ibv_close_device(ctx);
  return -errno;
}

int Rdma::add_remote_mr(PeerConfig &p, std::vector<LocalMR> &lmr, bool leader,
                        int port) {
  assert(lmr.size() == cq.size());

  size_t i;
  ibv_mr *m = nullptr;
  ibv_qp *q = nullptr;
  struct ibv_qp_attr a = {};
  struct ibv_qp_init_attr ia = {};
  unsigned mask = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;
  if (leader)
    mask |= IBV_ACCESS_REMOTE_WRITE;

  ia.cap.max_send_wr = MU_MAX_WR;
  ia.cap.max_recv_wr = MU_MAX_WR;
  ia.cap.max_send_sge = MU_MAX_SGE;
  ia.cap.max_recv_sge = MU_MAX_SGE;
  ia.qp_type = IBV_QPT_RC;

  a.qp_state = IBV_QPS_INIT;
  a.qp_access_flags = mask;
  a.pkey_index = 0;
  a.port_num = port;

  for (i = 0; i < lmr.size(); ++i) {
    if (!(m = ibv_reg_mr(pd, lmr[i].first, lmr[i].second, mask))) {
      MU_LOG_ERR("ibv_reg_mr failed");
      goto errmr;
    }
    ia.send_cq = ia.recv_cq = cq[i];
    if (!(q = ibv_create_qp(pd, &ia))) {
      MU_LOG_ERR("ibv_create_qp failed");
      goto errqp;
    }
    if (ibv_modify_qp(q, &a,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                          IBV_QP_ACCESS_FLAGS)) {

      MU_LOG_ERR("ibv_modify_qp failed");
      goto errmod;
    }
    mr[p.hash].push_back(std::make_pair(m, q));
    ConnectionAttr c{.addr = (uint64_t)m->addr,
                     .rkey = m->rkey,
                     .lid = portinfo.lid,
                     .qpn = q->qp_num,
                     .psn = 0,
                     .gid = {}};
    for (int k = 0; k < 16; ++k)
      c.gid[k] = gid.raw[k];
    attr[p.hash].push_back(c);
  }

  return 0;

errmod:
  ibv_destroy_qp(q);
errqp:
  ibv_dereg_mr(m);
errmr:
  for (size_t j = 0; j < i; ++j) {
    ibv_dereg_mr(mr[p.hash][j].first);
    ibv_destroy_qp(mr[p.hash][j].second);
  }
  return -errno;
}

Rdma::~Rdma() {
  for (auto &m : mr)
    for (auto &r : m.second) {
      ibv_dereg_mr(r.first);
      ibv_destroy_qp(r.second);
    }
  for (auto &c : cq)
    ibv_destroy_cq(c);
  ibv_dealloc_pd(pd);
  ibv_close_device(ctx);
}
