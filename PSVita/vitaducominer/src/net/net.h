#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

int net_init(void);
int net_connect(const char *host, uint16_t port);
int net_send(const uint8_t *buf, int len);
int net_recv(uint8_t *buf, int len);
int net_recv_line(uint8_t *buf, int buf_size);
void net_close(void);
bool net_is_connected(void);
const char *net_local_ip(void);

#endif
