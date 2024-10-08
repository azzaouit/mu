#ifndef CONFIG_H
#define CONFIG_H

#define NODES 2
#define MU_USE_ROCEE 1

struct peer {
  const char *addr;
  unsigned short port;
  unsigned short ib_port;
#ifdef MU_USE_ROCEE
  int gid_index; /* required for ROCEE */
#endif
};

static struct peer peers[] = {
    {.addr = "10.10.1.1",
     .port = 8000,
     .ib_port = 1,
#ifdef MU_USE_ROCEE
     .gid_index = 0
#endif
    },

    {.addr = "10.10.1.2",
     .port = 8000,
     .ib_port = 1,
#ifdef MU_USE_ROCEE
     .gid_index = 0
#endif
    },
};

#endif /* CONFIG_H */
