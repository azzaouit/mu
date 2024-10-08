#ifndef RDMA_H
#define RDMA_H

#include <infiniband/verbs.h>

#define IB_PORT 1

struct rdma_ctx {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_qp *qp;
  struct ibv_mr *mr;
  struct ibv_port_attr portinfo;
};


int rdma_ctx_init(struct rdma_ctx *, void *, size_t);

#endif /* RDMA_H */
