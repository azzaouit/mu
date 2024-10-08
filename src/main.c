#include <node.h>
#include <stdio.h>

int main() {
  int ret = 0;
  struct node *n = NULL;

  printf("Network Configuration\n");
  printf("=============================\n");
  for (int i = 0; i < NODES; ++i)
    printf("%s:%d\n", peers[i].addr, peers[i].port);
  printf("=============================\n");

  if ((ret = node_init(&n)) || !n)
    return ret;

  ret = node_run(n);
  node_destroy(&n);
  return ret;
}
