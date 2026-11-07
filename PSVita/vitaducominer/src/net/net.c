#include "net.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

static int g_sock = -1;
static char g_local_ip[16] = {0};
static bool g_connected = false;
static uint8_t g_recv_buffer[512];
static size_t g_recv_pos = 0;
static size_t g_recv_len = 0;

static int resolve_host(const char *host, uint32_t *ip) {
    SceNetInAddr addr;
    if (sceNetInetPton(SCE_NET_AF_INET, host, &addr) > 0) {
        *ip = ntohl(addr.s_addr);
        return 0;
    }
    int rid = sceNetResolverCreate("duco", NULL, 0);
    if (rid < 0) {
        return rid;
    }
    int ret = sceNetResolverStartNtoa(rid, host, &addr, 5000, 3, 0);
    if (ret < 0) {
        sceNetResolverDestroy(rid);
        return ret;
    }
    int result = 0;
    for (int i = 0; i < 200; i++) {
        ret = sceNetResolverGetError(rid, &result);
        if (ret < 0) {
            break;
        }
        if (result != 0) {
            break;
        }
        sceKernelDelayThread(10000);
    }
    if (result == 0) {
        *ip = ntohl(addr.s_addr);
        ret = 0;
    } else {
        ret = result;
    }
    sceNetResolverDestroy(rid);
    return ret;
}

int net_init(void) {
    static uint8_t net_mem[0x10000];
    SceNetInitParam param;
    memset(&param, 0, sizeof(param));
    param.memory = net_mem;
    param.size = sizeof(net_mem);
    param.flags = 0;
    int ret = sceNetInit(&param);
    if (ret < 0 && ret != -0x80410021) {
        return ret;
    }
    ret = sceNetCtlInit();
    if (ret < 0) {
        return ret;
    }
    SceNetCtlInfo info;
    memset(&info, 0, sizeof(info));
    ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
    if (ret >= 0) {
        snprintf(g_local_ip, sizeof(g_local_ip), "%s", info.ip_address);
    }
    return 0;
}

int net_connect(const char *host, uint16_t port) {
    if (!host || !host[0] || port == 0) return -1;
    net_close();

    uint32_t ip = 0;
    int ret = resolve_host(host, &ip);
    if (ret < 0) {
        return ret;
    }
    g_sock = sceNetSocket("duco", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (g_sock < 0) {
        return g_sock;
    }
    struct SceNetSockaddrIn addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons(port);
    addr.sin_addr.s_addr = htonl(ip);
    ret = sceNetConnect(g_sock, (struct SceNetSockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        sceNetSocketClose(g_sock);
        g_sock = -1;
        return ret;
    }
    g_recv_pos = 0;
    g_recv_len = 0;
    g_connected = true;
    return 0;
}

int net_send(const uint8_t *buf, int len) {
    if (!g_connected || g_sock < 0 || !buf || len < 0) {
        return -1;
    }
    int total = 0;
    while (total < len) {
        int sent = sceNetSend(g_sock, buf + total, len - total, 0);
        if (sent <= 0) return sent < 0 ? sent : -1;
        total += sent;
    }
    return total;
}

int net_recv(uint8_t *buf, int len) {
    if (!g_connected || g_sock < 0 || !buf || len <= 0) {
        return -1;
    }
    return sceNetRecv(g_sock, buf, len, 0);
}

int net_recv_line(uint8_t *buf, int buf_size) {
    if (!buf || buf_size < 2) return -1;
    int used = 0;
    bool overflow = false;

    while (g_connected && g_sock >= 0) {
        if (g_recv_pos == g_recv_len) {
            int received = sceNetRecv(g_sock, g_recv_buffer, sizeof(g_recv_buffer), 0);
            if (received <= 0) return received < 0 ? received : -1;
            g_recv_pos = 0;
            g_recv_len = (size_t)received;
        }

        while (g_recv_pos < g_recv_len) {
            uint8_t ch = g_recv_buffer[g_recv_pos++];
            if (ch == '\n') {
                if (used > 0 && buf[used - 1] == '\r') used--;
                buf[used] = '\0';
                return overflow ? -2 : used;
            }
            if (used < buf_size - 1) {
                buf[used++] = ch;
            } else {
                overflow = true;
            }
        }
    }
    return -1;
}

void net_close(void) {
    if (g_sock >= 0) {
        sceNetSocketClose(g_sock);
        g_sock = -1;
    }
    g_connected = false;
    g_recv_pos = 0;
    g_recv_len = 0;
}

bool net_is_connected(void) {
    return g_connected;
}

const char *net_local_ip(void) {
    return g_local_ip;
}
