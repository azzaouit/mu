#ifndef TCP_H
#define TCP_H

void *server_thread(void *);
void *client_thread(void *);
int find_host_ipv4(int *);

#endif /* TCP_H */
