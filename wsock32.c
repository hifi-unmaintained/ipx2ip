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

#define CNCNET_PORT 8054
#define MAX_BCAST   8

static unsigned int ip = INADDR_ANY;
static unsigned int bcast = INADDR_BROADCAST;
static bool porthack = true;
static bool compmode = false;
static struct sockaddr_in bcast_list[MAX_BCAST];

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        char buf[512];
        if (GetEnvironmentVariable("CNCNET_NODES", buf, sizeof buf) > 0)
        {
            dprintf("CnCNet 5 compatibility mode enabled:\n");

            int i = 0;
            char *p = strtok(buf, " ");
            while (p)
            {
                char *ip = p;
                char *port = strchr(p, ':');

                if (port != NULL && port - ip < strlen(p))
                {
                    *port++ = '\0';

                    bcast_list[i].sin_family = AF_INET;
                    bcast_list[i].sin_addr.s_addr = inet_addr(ip);
                    bcast_list[i].sin_port = htons(atoi(port));

                    if (ntohs(bcast_list[i].sin_port) != CNCNET_PORT)
                    {
                        porthack = false;
                    }

                    dprintf("  %s:%d\n", inet_ntoa(bcast_list[i].sin_addr), ntohs(bcast_list[i].sin_port));

                    i++;
                }

                p = strtok(NULL, " ");
            }

            dprintf("Port hack is %s.\n", porthack ? "enabled" : "disabled");

            compmode = true;
        }
    }

    return TRUE;
}

static void ipx2in(const struct sockaddr_ipx *from, struct sockaddr_in *to)
{
    memset(to, 0, sizeof *to);
    to->sin_family = AF_INET;
    memcpy(&to->sin_addr.s_addr, from->sa_nodenum, 4);
    to->sin_port = from->sa_socket;

    if (from->sa_nodenum[0] == -1 || from->sa_netnum[0] == -1)
    {
        to->sin_addr.s_addr = bcast;
    }
}

static void in2ipx(const struct sockaddr_in *from, struct sockaddr_ipx *to)
{
    memset(to, 0, sizeof *to);
    to->sa_family = AF_IPX;
    *(DWORD *)&to->sa_netnum = 0xDEADBEEF;
    memcpy(to->sa_nodenum, &from->sin_addr.s_addr, 4);
    to->sa_socket = from->sin_port;
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
        struct sockaddr_in name_in;

        ipx2in((const struct sockaddr_ipx *)name, &name_in);

        if (compmode)
        {
            name_in.sin_port = htons(CNCNET_PORT);
        }

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
                    ip = inet_addr(adapter->IpAddressList.IpAddress.String);
                    unsigned int mask = inet_addr(adapter->IpAddressList.IpMask.String);
                    bcast = (ip & mask) ^ (mask ^ 0xFFFFFFFF);

                    dprintf("    Adapter %lu:\n", adapter->Index);
                    dprintf("      ip  : %s\n", adapter->IpAddressList.IpAddress.String);
                    dprintf("      mask: %s\n", adapter->IpAddressList.IpMask.String);
                    dprintf("      bcast: %s\n", inet_ntoa(*(struct in_addr *)&bcast));

                    break;
                }

                adapter = adapter->Next;
            }
        }

        dprintf("  binding to %s:%d\n", inet_ntoa(name_in.sin_addr), ntohs(name_in.sin_port));
        return bind(s, (struct sockaddr *)&name_in, sizeof name_in);
    }

    return bind(s, (struct sockaddr *)name, namelen);
}

int WINAPI ipx_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr_ipx *from, int *fromlen)
{
    struct sockaddr_in from_in;
    int from_in_len = sizeof from_in;

    int ret = recvfrom(s, buf, len, flags, (struct sockaddr *)&from_in, &from_in_len);

    if (compmode && porthack)
    {
        from_in.sin_port = htons(CNCNET_PORT);
    }

    in2ipx(&from_in, from);

    dprintf("recvfrom(s=%d, buf=%p, len=%d, flags=%08X, from=%p, fromlen=%p (%d) -> %d (err: %d)\n", s, buf, len, flags, from, fromlen, *fromlen, ret, WSAGetLastError());

    return ret;
}

int WINAPI ipx_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr_ipx *to, int tolen)
{
    dprintf("sendto(s=%d, buf=%p, len=%d, flags=%08X, to=%p, tolen=%d)\n", s, buf, len, flags, to, tolen);

    if (to->sa_family == AF_IPX)
    {
        struct sockaddr_in to_in;

        ipx2in(to, &to_in);

        if (compmode && to_in.sin_addr.s_addr == bcast)
        {
            dprintf("  doing a compatibility broadcast\n");

            for (int i = 0; i < MAX_BCAST; i++)
            {
                if (bcast_list[i].sin_family)
                {
                    sendto(s, buf, len, flags, (struct sockaddr *)&bcast_list[i], sizeof bcast_list[i]);
                }
            }

            return len;
        }

        return sendto(s, buf, len, flags, (struct sockaddr *)&to_in, sizeof to_in);
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
    struct sockaddr_in name_in;
    int name_in_len = sizeof name_in;

    dprintf("getsockname(s=%d, name=%p, namelen=%p) (%d)\n", s, name, namelen, *namelen);

    int ret = getsockname(s, (struct sockaddr *)&name_in, &name_in_len);

    name_in.sin_addr.s_addr = ip;

    in2ipx(&name_in, name);

    return ret;
}
