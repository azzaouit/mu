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

/* Scan through connected interfaces and look
 * for our peer entry in the peer list */
int find_host_ipv4(int *p) {
  int sockfd, n, ret = 0;
  struct ifconf ifconf;
  struct ifreq ifr[MAX_IF];
  char ip[INET_ADDRSTRLEN];

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket:");
    return errno;
  }

  ifconf.ifc_buf = (char *)ifr;
  ifconf.ifc_len = sizeof ifr;

  if (ioctl(sockfd, SIOCGIFCONF, &ifconf) < 0) {
    perror("ioctl:");
    goto error;
  }

  n = ifconf.ifc_len / sizeof(ifr[0]);

  *p = -1;
  for (int i = 0; i < n; ++i) {
    struct sockaddr_in *s_in = (struct sockaddr_in *)&ifr[i].ifr_addr;
    if (!inet_ntop(AF_INET, &s_in->sin_addr, ip, sizeof(ip))) {
      perror("inet_ntop:");
      goto error;
    }
    for (int j = 0; j < NODES; ++j)
      if (!strcmp(ip, peers[j].addr)) {
        *p = j;
        goto done;
      }
  }

  errno = EINVAL;
error:
  ret = errno;
done:
  close(sockfd);
  return ret;
}

/* Find the global id of this host in the network configuration */
int peer_addr2id(struct node *n, char *addr) {
  for (int i = 0, j = 0; i < NODES; ++i) {
    if (!strcmp(peers[i].addr, addr))
      return j;
    j += (strcmp(peers[i].addr, n->host->addr) != 0);
  }
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
  char *hostaddrp, buf[PEER_INFO_LEN] = {0};
  int serverfd, clientfd, nbytes, optval = 1;
  struct node *n = ((struct thread_args *)ptr)->n;
  struct peer *p = ((struct thread_args *)ptr)->p;

  if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket:");
    pthread_exit(NULL);
  }

  setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

  bzero((char *)&server, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(p->port);

  if (bind(serverfd, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("bind:");
    goto err;
  }

  if (listen(serverfd, 5) < 0) {
    perror("listen:");
    goto err;
  }

  printf("[tcp/server] Server listening on %s:%d\n", p->addr, p->port);

  clientlen = sizeof(client);
  for (int npeers = 0; npeers < NODES - 1; ++npeers)
    if ((clientfd = accept(serverfd, (struct sockaddr *)&client, &clientlen)) <
        0)
      perror("accept:");
    else {
      hostaddrp = inet_ntoa(client.sin_addr);
      printf("[tcp/server] Established connection with %s\n", hostaddrp);

      int pid = peer_addr2id(n, hostaddrp);
      if (pid < 0) {
        fprintf(stderr, "[tcp/server] %s not found in network configuration.\n",
                hostaddrp);
        continue;
      }

      local_peer_info(n, &local, MU_REPLICATION_PLANE, pid);
      peer_info_pack(&local, buf);
      if ((nbytes = write(clientfd, buf, sizeof buf)) != PEER_INFO_LEN)
        perror("write:");

      printf("[tcp/server] (1/2) Sent %d bytes to %s\n", nbytes, hostaddrp);

      local_peer_info(n, &local, MU_BACKGROUND_PLANE, pid);
      peer_info_pack(&local, buf);
      if ((nbytes = write(clientfd, buf, sizeof buf)) != PEER_INFO_LEN)
        perror("write:");

      printf("[tcp/server] (2/2) Sent %d bytes to %s\n", nbytes, hostaddrp);

      close(clientfd);
    }

err:
  close(serverfd);
  pthread_exit((void *)&errno);
}

void *client_thread(void *ptr) {
  int i, sockfd, nbytes;
  struct peer_info *remote_pi;
  struct sockaddr_in serveraddr;
  char buf[PEER_INFO_LEN] = {0};
  struct peer *p = ((struct thread_args *)ptr)->p;

  if (!(remote_pi = malloc(sizeof(struct peer_info) * 2))) {
    perror("malloc:");
    pthread_exit(NULL);
  }

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
    goto err;
  }

  if ((nbytes = read(sockfd, buf, PEER_INFO_LEN)) != PEER_INFO_LEN) {
    perror("read:");
    goto err;
  }
  peer_info_unpack(buf, remote_pi);
  fprintf(stderr, "[tcp/client] (1/2) Read %d bytes from %s:%d.\n", nbytes,
          p->addr, p->port);

  if ((nbytes = read(sockfd, buf, PEER_INFO_LEN)) != PEER_INFO_LEN) {
    perror("read:");
    goto err;
  }
  peer_info_unpack(buf, remote_pi + 1);
  fprintf(stderr, "[tcp/client] (2/2) Read %d bytes from %s:%d.\n", nbytes,
          p->addr, p->port);

  close(sockfd);
  pthread_exit(remote_pi);

err:
  free(remote_pi);
  close(sockfd);
  pthread_exit(NULL);
}
