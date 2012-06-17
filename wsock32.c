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

#ifdef _DEBUG
    #include <stdio.h>
    #define dprintf(...) fprintf(stderr, __VA_ARGS__)
#else
    #define dprintf(...)
#endif

static unsigned int ip = INADDR_ANY;
static unsigned int bcast = INADDR_BROADCAST;

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
