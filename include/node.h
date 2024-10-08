#ifndef NODE_H
#define NODE_H

#include <config.h>
#include <infiniband/verbs.h>

#define MAX_RECV_WR 10
#define MU_LOG_SIZE (1 << 10) /* default node log size */

#ifdef MU_USE_ROCEE
#define PEER_INFO_LEN 34   /* sizeof peer_info */
#else
#define PEER_INFO_LEN 18   /* sizeof peer_info */
#endif

/* Main node handle */
struct node {
  struct peer *host;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_mr *mr;
  struct ibv_context *ctx;
  struct ibv_qp *qp[NODES - 1];
  struct ibv_port_attr portinfo;
  union ibv_gid gid; /* Required for ROCEE */
  char log[];
};

/* Sent over TCP to negotiate RDMA connection */
struct peer_info {
  uint64_t addr;
  uint32_t rkey;
  uint32_t qp_num;
  uint16_t lid;
  uint8_t gid[16];
} __attribute__((packed));

/* Passed to server_thread and client_thread */
struct thread_args {
  struct node *n;
  struct peer *p;
};

/* Initialize a node */
int node_init(struct node **);

/* Main entry into the protocol. Will block for the duration of the run */
int node_run(struct node *);

/* Release any resources held by the node handler */
void node_destroy(struct node **);

/* Serialize a peer info for network tx */
void peer_info_pack(struct peer_info *, char *);

/* Deserialize a peer info after a network rx */
void peer_info_unpack(char *, struct peer_info *);

#endif /* NODE_H */
