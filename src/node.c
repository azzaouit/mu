#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rdma.h"

int main() {
  long page_size = sysconf(_SC_PAGESIZE);
  void *buf = malloc(page_size);

  struct rdma_ctx ctx;

  int ret = rdma_ctx_init(&ctx, buf, page_size);

  if (ret) {
    fprintf(stderr, "Failed to init RDMA context\n");
  }

  free(buf);
  return ret;
}
