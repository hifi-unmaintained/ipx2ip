/*
 * Copyright (c) 2010, 2011, 2012 Toni Spets <toni.spets@iki.fi>
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

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <iphlpapi.h>
#include "node.h"

#ifdef _DEBUG
    #include <stdio.h>
    #define dprintf(...) fprintf(stderr, __VA_ARGS__)
#else
    #define dprintf(...)
#endif

static unsigned char uid[6];
static int ipx_sock = 0;

static unsigned int broadcast[16];

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        UUID uuid;
        int i;

        UuidCreate(&uuid);
        for (i = 0; i < 6; i++)
            uid[i] = uuid.Data4[i];

        dprintf("Virtual MAC address is: %02X:%02X:%02X:%02X:%02X:%02X\r\n", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);

        memset(&broadcast, 0, sizeof broadcast);
    }

    return TRUE;
}

SOCKET WINAPI ipx_socket(int af, int type, int protocol)
{
    dprintf("socket(af=%08X, type=%08X, protocol=%08X)\n", af, type, protocol);

    if (af == AF_IPX)
    {
        return socket(AF_INET, SOCK_DGRAM, 0);
    }

    return socket(af, type, protocol);
}

int WINAPI ipx_bind(SOCKET s, const struct sockaddr_ipx *name, int namelen)
{
    dprintf("bind(s=%d, name=%p, namelen=%d)\n", s, name, namelen);

    if (((struct sockaddr_ipx *)name)->sa_family == AF_IPX)
    {
        DWORD ret;
        ULONG adapters_size = 0;
        int i = 0;
        struct sockaddr_in name_in;

        /*ipx2in((const struct sockaddr_ipx *)name, &name_in);*/
        memset(&name_in, 0, sizeof name_in);
        name_in.sin_family = AF_INET;
        name_in.sin_addr.s_addr = INADDR_ANY;
        name_in.sin_port = name->sa_socket;

        GetAdaptersInfo(NULL, &adapters_size);

        PIP_ADAPTER_INFO adapters = malloc(adapters_size);

        ret = GetAdaptersInfo(adapters, &adapters_size);

        if (ret == ERROR_SUCCESS)
        {
            dprintf("  Found %lu adapters\n", (adapters_size / sizeof *adapters));
            PIP_ADAPTER_INFO adapter = adapters;
            while (adapter)
            {
                if (adapter->IpAddressList.IpAddress.String[0])
                {
                    PIP_ADDR_STRING addr = &adapter->IpAddressList;

                    dprintf("    Adapter %lu:\n", adapter->Index);

                    while (addr)
                    {
                        unsigned int ip = inet_addr(addr->IpAddress.String);
                        unsigned int mask = inet_addr(addr->IpMask.String);
                        unsigned int bcast = (ip & mask) ^ (mask ^ 0xFFFFFFFF);

                        dprintf("      ip  : %s\n", addr->IpAddress.String);
                        dprintf("      mask: %s\n", addr->IpMask.String);
                        dprintf("      bcast: %s\n", inet_ntoa(*(struct in_addr *)&bcast));

                        broadcast[i++] = bcast;

                        addr = addr->Next;
                    }
                }

                adapter = adapter->Next;
            }
        }

        dprintf("  binding to %s:%d\n", inet_ntoa(name_in.sin_addr), ntohs(name_in.sin_port));
        ipx_sock = s; /* for ipx_getsockname() */
        return bind(s, (struct sockaddr *)&name_in, sizeof name_in);
    }

    return bind(s, (struct sockaddr *)name, namelen);
}

int WINAPI ipx_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr_ipx *from, int *fromlen)
{
    struct sockaddr_in from_in;
    int from_in_len = sizeof from_in;

    static char ibuf[4096];

    int ret = recvfrom(s, ibuf, 4096, flags, (struct sockaddr *)&from_in, &from_in_len);

    if (ret > 6)
    {
        struct node *n = node_from_ip(&from_in);

        if (!n)
        {
            n = node_new(ibuf, &from_in);

            // out of memory
            if (!n)
                return 0;

            node_insert(n);
        }

        from->sa_family = AF_IPX;
        *(DWORD *)from->sa_netnum = 0xDEADBEEF;
        memcpy(from->sa_nodenum, n->mac, sizeof from->sa_nodenum);
        from->sa_socket = n->in.sin_port;
        memcpy(buf, ibuf + 6, ret - 6);

        dprintf("recvfrom(s=%d, buf=%p, len=%d, flags=%08X, from=%p, fromlen=%p (%d) -> %d (err: %d)\n", s, buf, len, flags, from, fromlen, *fromlen, ret - 6, WSAGetLastError());

        return ret - 6;
    }

    return ret;
}

int WINAPI ipx_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr_ipx *to, int tolen)
{
    dprintf("sendto(s=%d, buf=%p, len=%d, flags=%08X, to=%p, tolen=%d)\n", s, buf, len, flags, to, tolen);

    if (to->sa_family == AF_IPX)
    {
        static char obuf[4096];
        struct sockaddr_in to_in;
        memset(&to_in, 0, sizeof to_in);

        memcpy(obuf, uid, 6);
        memcpy(obuf+6, buf, len);

        if (to->sa_nodenum[0] == -1 || to->sa_netnum[0] == -1)
        {
            int i;

            to_in.sin_family = AF_INET;
            to_in.sin_port = to->sa_socket;

            for (i = 0; i < (sizeof broadcast) / 4; i++)
            {
                if (broadcast[i] == 0)
                    break;

                to_in.sin_addr.s_addr = broadcast[i];

                sendto(s, obuf, len + 6, flags, (struct sockaddr *)&to_in, sizeof to_in);
            }

            return 0;
        }
        else
        {
            struct node *n = node_from_mac(to->sa_nodenum);

            if (n)
            {
                memcpy(&to_in, &n->in, sizeof to_in);
            }
        }

        return sendto(s, obuf, len + 6, flags, (struct sockaddr *)&to_in, sizeof to_in);
    }

    return sendto(s, buf, len, flags, (struct sockaddr *)to, tolen);
}

int WINAPI ipx_getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
    dprintf("getsockopt(s=%d, level=%08X, optname=%08X, optval=%p, optlen=%p)\n", s, level, optname, optval, optlen);

    if (level == 0x3E8)
    {
        level = 0xFFFF;
    }

    return getsockopt(s, level, optname, optval, optlen);
}

int WINAPI ipx_setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
    dprintf("setsockopt(s=%d, level=%08X, optname=%08X, optval=%p, optlen=%d)\n", s, level, optname, optval, optlen);

    if (level == 0x3E8)
    {
        level = 0xFFFF;
    }

    return setsockopt(s, level, optname, optval, optlen);
}

int WINAPI ipx_getsockname(SOCKET s, struct sockaddr_ipx *name, int *namelen)
{
    dprintf("getsockname(s=%d, name=%p, namelen=%p) (%d)\n", s, name, namelen, *namelen);

    /* very unlikely to fail, if it's an issue, I'll address it later  */
    if (s == ipx_sock)
    {
        struct sockaddr_in name_in;
        int name_in_len = sizeof name_in;

        getsockname(s, (struct sockaddr *)&name_in, &name_in_len);

        name->sa_family = AF_IPX;
        *(DWORD *)name->sa_netnum = 0xDEADBEEF;
        memcpy(name->sa_nodenum, uid, sizeof name->sa_nodenum);
        name->sa_socket = name_in.sin_port;
    }

    return getsockname(s, (struct sockaddr *)name, namelen);;
}
