#ifndef CONFIG_H
#define CONFIG_H

#define MU_PEERS ((int)(sizeof(peers) / sizeof(peers[0])))
#define MU_USE_ROCEE 1 /* 1 for RoCEE, 0 for IB */

/* Default port config for this node */
#define MU_TCP_PORT 8000
#define MU_IB_PORT 1
#define MU_GID_INDEX 0

struct peer {
  const char *addr;
  unsigned short port;
  unsigned short ib_port;
#ifdef MU_USE_ROCEE
  int gid_index; /* required for RoCEE */
#endif
};

static struct peer peers[] = {
    {.addr = "10.10.1.2",
     .port = 8000,
     .ib_port = 1,
#ifdef MU_USE_ROCEE
     .gid_index = 0
#endif
    },
};

#endif /* CONFIG_H */
