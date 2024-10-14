#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <linux/netdevice.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <node.h>

#define MAX_IF (1 << 3)
#define MAX_RETRIES 5

/* Validate a connecting peer by checking the peer list */
int valid_peer(const char *p) {
  for (int i = 0; i < MU_PEERS; ++i)
    if (!strcmp(p, peers[i].addr))
      return i;
  return -1;
}

/* Extract peer info for this host to send over to a client */
void local_peer_info(struct node *n, struct peer_info *p, int plane,
                     int qp_id) {
  p->addr =
      (plane == MU_REPLICATION_PLANE ? (uint64_t)&n->log : (uint64_t)&n->bg);
  p->rkey = n->mr[plane]->rkey;
  p->qp_num = n->qp[plane][qp_id]->qp_num;
  p->lid = n->portinfo.lid;
  for (int i = 0; i < 16; ++i)
    p->gid[i] = n->gid.raw[i];
}

void *server_thread(void *ptr) {
  socklen_t clientlen;
  struct peer_info local;
  struct sockaddr_in server, client;
  struct node *n = (struct node *)ptr;
  char *hostaddrp, buf[PEER_INFO_LEN] = {0};
  int pid, serverfd, clientfd, nbytes, optval = 1;

  if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket:");
    pthread_exit(NULL);
  }

  setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

  bzero((char *)&server, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(MU_TCP_PORT);

  if (bind(serverfd, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("bind:");
    goto err;
  }

  if (listen(serverfd, 5) < 0) {
    perror("listen:");
    goto err;
  }

  printf("[tcp/server] Server listening on port %d\n", MU_TCP_PORT);

  clientlen = sizeof(client);
  for (int npeers = 0; npeers < MU_PEERS; ++npeers)
    if ((clientfd = accept(serverfd, (struct sockaddr *)&client, &clientlen)) <
        0)
      perror("accept:");
    else {
      hostaddrp = inet_ntoa(client.sin_addr);
      printf("[tcp/server] Established connection with %s\n", hostaddrp);

      if ((pid = valid_peer(hostaddrp)) < 0) {
        fprintf(stderr, "[tcp/server] %s not found in network configuration.\n",
                hostaddrp);
        continue;
      }

      local_peer_info(n, &local, MU_REPLICATION_PLANE, pid);
      peer_info_pack(&local, buf);
      if ((nbytes = write(clientfd, buf, sizeof buf)) != PEER_INFO_LEN)
        perror("write:");

      local_peer_info(n, &local, MU_BACKGROUND_PLANE, pid);
      peer_info_pack(&local, buf);
      if ((nbytes = write(clientfd, buf, sizeof buf)) != PEER_INFO_LEN)
        perror("write:");

      close(clientfd);
    }

err:
  close(serverfd);
  pthread_exit((void *)&errno);
}

void *client_thread(void *ptr) {
  int i, sockfd, nbytes;
  struct sockaddr_in serveraddr;
  char buf[PEER_INFO_LEN] = {0};
  struct node *n = ((struct client_args *)ptr)->n;
  int id = ((struct client_args *)ptr)->id;
  int *ret = &((struct client_args *)ptr)->ret;
  struct peer *p = peers + id;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket:");
    pthread_exit(NULL);
  }

  bzero(&serveraddr, sizeof serveraddr);
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = inet_addr(p->addr);
  serveraddr.sin_port = htons(p->port);

  printf("[tcp/client] Trying to connect to %s:%d\n", p->addr, p->port);

  for (i = 0; i < MAX_RETRIES; ++i) {
    sleep(5);
    if (!connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) {
      printf("[tcp/client] Established connection with %s:%d\n", p->addr,
             p->port);
      break;
    } else {
      perror("connect:");
      fprintf(stderr, "[tcp/client] Connection failed. Retrying %d of %d...\n",
              i + 1, MAX_RETRIES);
    }
  }

  if (i >= MAX_RETRIES) {
    fprintf(stderr, "[tcp/client] %s:%d unreachable.\n", p->addr, p->port);
    *ret = 1;
    goto exit;
  }

  if ((nbytes = read(sockfd, buf, PEER_INFO_LEN)) != PEER_INFO_LEN) {
    perror("read:");
    *ret = errno;
    goto exit;
  }
  peer_info_unpack(buf, n->pi[id] + MU_REPLICATION_PLANE);

  if ((nbytes = read(sockfd, buf, PEER_INFO_LEN)) != PEER_INFO_LEN) {
    perror("read:");
    *ret = errno;
    goto exit;
  }
  peer_info_unpack(buf, n->pi[id] + MU_BACKGROUND_PLANE);

  if (node_connect(n, MU_REPLICATION_PLANE, id)) {
    fprintf(stderr, "[tcp/client] Replication QP Connection failed.\n");
    *ret = 2;
    goto exit;
  }

  if (node_connect(n, MU_BACKGROUND_PLANE, id)) {
    fprintf(stderr, "[tcp/client] Background QP Connection failed.\n");
    *ret = 3;
    goto exit;
  }

exit:
  close(sockfd);
  pthread_exit(NULL);
}
