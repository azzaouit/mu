#ifndef NODE_H
#define NODE_H

#include <config.h>
#include <infiniband/verbs.h>

#ifdef MU_USE_ROCEE
#define PEER_INFO_LEN 34 /* sizeof peer_info */
#else
#define PEER_INFO_LEN 18 /* sizeof peer_info */
#endif

#define MAX_RECV_WR 10
#define MU_LOG_SIZE (1 << 10)
#define MU_MAX_SLOTS (1 << 10)
#define MU_REPLICATION_PLANE 0
#define MU_BACKGROUND_PLANE 1

/* Slot entry */
struct slot {
  uint32_t propno;
  int32_t value;
} __attribute__((packed));

/* Replication log. Invariant: only one replica has write access. */
struct log {
  /* Smallest proposal number with which a leader
   * may enter the accept phase on this replica */
  int min_proposal;
  /* Lowest log index this replica believes to be undecided */
  int FUO;
  /* Sequence of slots */
  struct slot slots[MU_MAX_SLOTS];
};

/* Background context. All replicas have read/write access.*/
struct background {
  /* Read by remote replicas to determine heartbeat. */
  uint64_t heart;
  /* Write by remote replicas to request log write permissions */
  uint8_t perms[MU_PEERS];
};

/* Sent over TCP to connect a QP */
struct peer_info {
  uint64_t addr;
  uint32_t rkey;
  uint32_t qp_num;
  uint16_t lid;
  uint8_t gid[16];
} __attribute__((packed));

/* Main node handle */
struct node {
  /* IB context */
  struct ibv_context *ctx;
  /* Protection domain used for both memory regions */
  struct ibv_pd *pd;
  /* MRs for replication/background planes */
  struct ibv_mr *mr[2];
  /* Shared CQs for replication/background planes */
  struct ibv_cq *cq[2];
  /* QPs for replication/background planes */
  struct ibv_qp *qp[2][MU_PEERS];
  /* IB Port attributes */
  struct ibv_port_attr portinfo;
#ifdef MU_USE_ROCEE
  /* Required for ROCEE */
  union ibv_gid gid;
#endif
  /* Remote peer info: memory regions for remote ops */
  struct peer_info pi[MU_PEERS][2];
  /* Replication log for this replica. */
  struct log log;
  /* Metadata for this replica */
  struct background bg;
};

/* Passed to client_thread */
struct client_args {
  struct node *n;
  int id;
  int ret;
};

/* Initialize a node */
int node_init(struct node *);

/* Connect to remote QP */
int node_connect(struct node *n, int plane, int id);

/* Main entry into the protocol. Will block for the duration of the protocol */
int node_run(struct node *);

/* Release any resources held by the node */
void node_destroy(struct node *);

/* Serialize a peer info for network tx */
void peer_info_pack(struct peer_info *, char *);

/* Deserialize a peer info after a network rx */
void peer_info_unpack(char *, struct peer_info *);

#endif /* NODE_H */
