/*
 * Copyright (c) 2010, 2011, 2012, 2013 Toni Spets <toni.spets@iki.fi>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <winsock2.h>
#include <windows.h>
#include <wsipx.h>
#include <iphlpapi.h>
#include <stdbool.h>

#ifdef _DEBUG
    #include <stdio.h>
    #define dprintf(...) fprintf(stderr, __VA_ARGS__)
#else
    #define dprintf(...)
#endif

SOCKET WINAPI ipx_socket(int af, int type, int protocol);
int WINAPI ipx_bind(SOCKET s, const struct sockaddr_ipx *name, int namelen);
int WINAPI ipx_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr_ipx *from, int *fromlen);
int WINAPI ipx_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr_ipx *to, int tolen);
int WINAPI ipx_getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen);
int WINAPI ipx_setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen);
int WINAPI ipx_getsockname(SOCKET s, struct sockaddr_ipx *name, int *namelen);

static int net_socket = 0;
static int port = 0;

int WINAPI _IPX_Initialise()
{
    dprintf("_IPX_Initialise()\n");
    return 1;
}

int WINAPI _IPX_Open_Socket95(int s)
{
    dprintf("_IPX_Open_Socket95(s=%d)\n", s);

    if (net_socket)
    {
        closesocket(net_socket);
        net_socket = 0;
    }

    net_socket = ipx_socket(AF_IPX, 0, 0);

    port = 1;
    setsockopt(net_socket, SOL_SOCKET, SO_BROADCAST, (void *)&port, sizeof port);
    port = s;

    struct sockaddr_ipx tmp;
    tmp.sa_family = AF_IPX;
    memset(tmp.sa_nodenum, 0, sizeof tmp.sa_nodenum);
    tmp.sa_socket = htons(s);

    ipx_bind(net_socket, &tmp, sizeof tmp);

    return 0;
}

int WINAPI _IPX_Start_Listening95()
{
    dprintf("_IPX_Start_Listening95()\n");
    return 1;
}

int WINAPI _IPX_Get_Outstanding_Buffer95(void *ptr)
{
    dprintf("_IPX_Get_Outstanding_Buffer95(ptr=%p)\n", ptr);

    /* didn't bother to look up what kind of stuff is in the header so just nullin' it */
    memset(ptr, 0, 30);

    /* using some old magic */
    short *len = ptr + 2;
    int *from = ptr + 22;
    short *port = ptr + 26;
    char *buf = ptr + 30;

    struct sockaddr_ipx ipx_from;
    struct timeval tv;
    int ret;

    if (!net_socket)
    {
        return 0;
    }

    fd_set read_fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&read_fds);
    FD_SET(net_socket, &read_fds);

    if (select(net_socket+1, &read_fds, NULL, NULL, &tv))
    {
        int ipx_len = sizeof (struct sockaddr_ipx);
        ret = ipx_recvfrom(net_socket, buf, 900, 0, &ipx_from, &ipx_len);

        if (ret > 0)
        {
            *len = htons(ret + 30);
            memcpy(from, ipx_from.sa_nodenum, 4);
            *port = ipx_from.sa_socket;
            return 1;
        }
    }

    return 0;
}

int WINAPI _IPX_Broadcast_Packet95(void *buf, int len)
{
    dprintf("_IPX_Broadcast_Packet95(buf=%p, len=%d)\n", buf, len);

    struct sockaddr_ipx to;
    to.sa_family = AF_IPX;
    to.sa_socket = htons(port);
    memset(&to.sa_nodenum, 0xFF, sizeof to.sa_nodenum);

    return ipx_sendto(net_socket, buf, len, 0, &to, sizeof to);
}

int WINAPI _IPX_Send_Packet95(void *ptr, void *buf, int len, char *unk1, void *unk2)
{
    dprintf("_IPX_Send_Packet95(ptr=%p, buf=%p, len=%d, unk1=%p, unk2=%p)\n", ptr, buf, len, unk1, unk2);

    struct sockaddr_ipx to;
    memset(&to, 0, sizeof to);
    to.sa_family = AF_IPX;
    memcpy(&to.sa_nodenum, ptr, 4);
    memcpy(&to.sa_socket, ptr + 4, 2);

    return ipx_sendto(net_socket, buf, len, 0, &to, sizeof (struct sockaddr_ipx));
}

int WINAPI _IPX_Get_Connection_Number95()
{
    dprintf("_IPX_Get_Connection_Number95()\n");
    return 0;
}

int WINAPI _IPX_Get_Local_Target95(void *p1, void *p2, void *p3, void *p4)
{
    dprintf("_IPX_Get_Local_Target95(p1=%p, p2=%p, p3=%p, p4=%p)\n", p1, p2, p3, p4);
    return 1;
}

int WINAPI _IPX_Close_Socket95(int s)
{
    dprintf("_IPX_Close_Socket95(s=%d)\n", s);
    return 0;
}

int WINAPI _IPX_Shut_Down95()
{
    dprintf("_IPX_Shut_Down95()\n");
    closesocket(net_socket);
    net_socket = 0;
    return 1;
}
