#include <errno.h>
#include <error.h>
#include <stdio.h>

#include <rdma.h>

int rdma_ctx_init(struct rdma_ctx *ctx, void *buf, size_t len) {
  struct ibv_device **dev_list;
  dev_list = ibv_get_device_list(NULL);
  if (!dev_list) {
    perror("ibv_get_device_list");
    return errno;
  }

  printf("Opening device: %s\n", ibv_get_device_name(dev_list[0]));

  ctx->ctx = ibv_open_device(dev_list[0]);
  if (!ctx) {
    perror("ibv_open_device");
    return errno;
  }

  ibv_free_device_list(dev_list);

  ctx->pd = ibv_alloc_pd(ctx->ctx);
  if (!ctx->pd) {
    perror("ibv_alloc_pd");
    return errno;
  }

  ctx->cq = ibv_create_cq(ctx->ctx, len + 1, NULL, NULL, 0);
  if (!ctx->cq) {
    perror("ibv_create_cq");
    return errno;
  }

  struct ibv_qp_init_attr init_attr = {.send_cq = ctx->cq,
                                       .recv_cq = ctx->cq,
                                       .cap = {.max_send_wr = 1,
                                               .max_recv_wr = len,
                                               .max_send_sge = 1,
                                               .max_recv_sge = 1},
                                       .qp_type = IBV_QPT_RC};

  ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
  if (!ctx->qp) {
    perror("ibv_create_qp");
    return errno;
  }

  struct ibv_qp_attr attr = {.qp_state = IBV_QPS_INIT,
                             .pkey_index = 0,
                             .port_num = IB_PORT,
                             .qp_access_flags = 0};

  if (ibv_modify_qp(ctx->qp, &attr,
                    IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                        IBV_QP_ACCESS_FLAGS)) {
    perror("ibv_modify_qp");
    return errno;
  }

  ctx->mr = ibv_reg_mr(ctx->pd, buf, len, IBV_ACCESS_LOCAL_WRITE);
  if (!ctx->mr) {
    perror("ibv_reg_mr");
    return errno;
  }

  return 0;
}

int rdma_ctx_connect(struct rdma_ctx *ctx, int dest_qp_num, int dlid) {
  struct ibv_qp_attr rtr_attr = {
      .qp_state = IBV_QPS_RTR,
      .path_mtu = IBV_MTU_1024,
      .rq_psn = 0,
      .max_dest_rd_atomic = 1,
      .min_rnr_timer = 0x12,
      .ah_attr.is_global = 0,
      .ah_attr.sl = 0,
      .ah_attr.src_path_bits = 0,
      .ah_attr.port_num = IB_PORT,
      .dest_qp_num = dest_qp_num,
      .ah_attr.dlid = dlid,
  };

  int ret = ibv_modify_qp(ctx->qp, &rtr_attr,
                          IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                              IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                              IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
  if (ret) {
    perror("ibv_modify_qp");
  }

  return ret;
}
