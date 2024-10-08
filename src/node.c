#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <node.h>
#include <tcp.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htonll(x) (((uint64_t)htonl(x) << 32) | htonl((x) >> 32))
#define ntohll(x) (((uint64_t)ntohl(x) << 32) | ntohl((x) >> 32))
#else
#define htonll(x) (x)
#define ntohll(x) (x)
#endif

/* Connect to remote QP */
int node_connect(struct node *n, struct peer_info *p, int id) {
  int ret;
  struct ibv_qp_attr rtr_attr = {
      .qp_state = IBV_QPS_RTR,
      .path_mtu = IBV_MTU_1024,
      .rq_psn = 0,
      .max_dest_rd_atomic = 1,
      .min_rnr_timer = 0x12,
      .ah_attr.is_global = 0,
      .ah_attr.sl = 0,
      .ah_attr.src_path_bits = 0,
      .ah_attr.port_num = n->host->ib_port,
      .dest_qp_num = p->qp_num,
      .ah_attr.dlid = p->lid,
  };

#ifdef MU_USE_ROCEE
  rtr_attr.ah_attr.is_global = 1;
  rtr_attr.ah_attr.grh.flow_label = 0;
  rtr_attr.ah_attr.grh.hop_limit = 1;
  rtr_attr.ah_attr.grh.sgid_index = n->host->gid_index;
  rtr_attr.ah_attr.grh.traffic_class = 0;
  for (int i = 0; i < 16; ++i)
    rtr_attr.ah_attr.grh.dgid.raw[i] = p->gid[i];
#endif

  if ((ret = ibv_modify_qp(n->qp[id], &rtr_attr,
                           IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                               IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                               IBV_QP_MAX_DEST_RD_ATOMIC |
                               IBV_QP_MIN_RNR_TIMER)))
    return ret;

  struct ibv_qp_attr rts_attr;
  memset(&rts_attr, 0, sizeof(rts_attr));
  rts_attr.qp_state = IBV_QPS_RTS;
  rts_attr.timeout = 0x12;
  rts_attr.retry_cnt = 7;
  rts_attr.rnr_retry = 7;
  rts_attr.sq_psn = 0;
  rts_attr.max_rd_atomic = 1;

  return ibv_modify_qp(n->qp[id], &rts_attr,
                       IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                           IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                           IBV_QP_MAX_QP_RD_ATOMIC);
}

/* Free any resources */
void node_destroy(struct node **node) {
  struct node *n = *node;
  for (int i = 0; i < NODES - 1; ++i)
    ibv_destroy_qp(n->qp[i]);
  ibv_dereg_mr(n->mr);
  ibv_destroy_cq(n->cq);
  ibv_dealloc_pd(n->pd);
  ibv_close_device(n->ctx);
  free(node);
}

/* Serialize peer info. Buffer must be at least of size PEER_INFO_LEN */
void peer_info_pack(struct peer_info *p, char *buf) {
  struct peer_info np;
  np.addr = htonll(p->addr);
  np.rkey = htonl(p->rkey);
  np.qp_num = htonl(p->qp_num);
  np.lid = htonl(p->lid);
#ifdef MU_USE_ROCEE
  for (int i = 0; i < 16; ++i)
    np.gid[i] = p->gid[i];
#endif
  memcpy(buf, &np, PEER_INFO_LEN);
}

/* Deserialize peer info. Buffer must be at least of size PEER_INFO_LEN */
void peer_info_unpack(char *buf, struct peer_info *p) {
  memcpy(p, buf, PEER_INFO_LEN);
  p->addr = ntohll(p->addr);
  p->rkey = ntohl(p->rkey);
  p->qp_num = ntohl(p->qp_num);
  p->lid = ntohl(p->lid);
}

/* Initialize a node */
int node_init(struct node **node) {
  struct node *n;
  int i, local, ret;
  struct ibv_device **dev_list;

  if (!(*node = malloc(sizeof(struct node) + MU_LOG_SIZE))) {
    perror("malloc:");
    return errno;
  }

  if ((ret = find_host_ipv4(&local)) || local < 0) {
    fprintf(stderr, "Host not found in configuration.");
    return ret;
  }

  n = *node;
  n->host = peers + local;

  if (!(dev_list = ibv_get_device_list(NULL))) {
    fprintf(stderr, "ibv_get_device_list failed");
    return errno;
  }

  n->ctx = ibv_open_device(dev_list[0]);
  if (!n->ctx) {
    fprintf(stderr, "ibv_open_device failed");
    return errno;
  }

  printf("[rdma] Opening device: %s\n", ibv_get_device_name(dev_list[0]));
  ibv_free_device_list(dev_list);

#ifdef MU_USE_ROCEE
  if (ibv_query_gid(n->ctx, n->host->ib_port, n->host->gid_index, &n->gid)) {
    fprintf(stderr, "ibv_query_gid failed\n");
    return errno;
  }
#endif

  if (ibv_query_port(n->ctx, n->host->ib_port, &n->portinfo)) {
    fprintf(stderr, "ibv_query_port failed\n");
    return errno;
  }

  n->pd = ibv_alloc_pd(n->ctx);
  if (!n->pd) {
    fprintf(stderr, "ibv_alloc_pd failed");
    return errno;
  }

  n->mr = ibv_reg_mr(n->pd, n->log, MU_LOG_SIZE, IBV_ACCESS_LOCAL_WRITE);
  if (!n->mr) {
    fprintf(stderr, "ibv_reg_mr failed");
    return errno;
  }

  n->cq = ibv_create_cq(n->ctx, 10, NULL, NULL, 0);
  if (!n->cq) {
    fprintf(stderr, "ibv_create_cq failed");
    return errno;
  }

  struct ibv_qp_init_attr init_attr = {.send_cq = n->cq,
                                       .recv_cq = n->cq,
                                       .cap = {.max_send_wr = 1,
                                               .max_recv_wr = MAX_RECV_WR,
                                               .max_send_sge = 1,
                                               .max_recv_sge = 1},
                                       .qp_type = IBV_QPT_RC};

  struct ibv_qp_attr attr = {.qp_state = IBV_QPS_INIT,
                             .pkey_index = 0,
                             .port_num = n->host->ib_port,
                             .qp_access_flags = 0};

  for (i = 0; i < NODES - 1; ++i) {
    n->qp[i] = ibv_create_qp(n->pd, &init_attr);
    if (!n->qp[i]) {
      fprintf(stderr, "ibv_create_qp failed");
      return errno;
    }

    if (ibv_modify_qp(n->qp[i], &attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                          IBV_QP_ACCESS_FLAGS)) {
      fprintf(stderr, "ibv_modify_qp failed");
      return errno;
    }
  }

  return 0;
}

/* Run node (blocking) */
int node_run(struct node *n) {
  int ret = 0;
  pthread_t st, ct[NODES - 1];
  struct thread_args a[NODES];
  struct peer_info *info[NODES - 1];

  a[0].n = n;
  a[0].p = n->host;
  if (pthread_create(&st, NULL, server_thread, (void *)a)) {
    perror("pthread_create:");
    return errno;
  }

  for (int i = 0, j = 0; i < NODES; ++i)
    if (peers + i != n->host) {
      a[j + 1].n = n;
      a[j + 1].p = peers + i;
      if (pthread_create(ct + j, NULL, client_thread, (void *)(a + j + 1))) {
        perror("pthread_create:");
        return errno;
      }
      ++j;
    }

  for (int i = 0; i < NODES - 1; ++i) {
    pthread_join(ct[i], (void **)(info + i));
    if (!info[i])
      return errno;
    if ((ret = node_connect(n, info[i], i)))
      return ret;
    printf("[node] Established RDMA connection with peer %s on QP %d\n",
           a[i + 1].p->addr, n->qp[i]->qp_num);
  }

  /* Main server loop blocks here */
  pthread_join(st, (void **)&ret);

  return ret;
}
