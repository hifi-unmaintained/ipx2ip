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
#include <stdbool.h>

#ifdef _DEBUG
    #include <stdio.h>
    #define dprintf(...) fprintf(stderr, __VA_ARGS__)
#else
    #define dprintf(...)
#endif

BOOL WINAPI DllMainCRTStartup(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) { return TRUE; }

static u_long   tunnel_ip       = 0;
static short    port            = 0;
static int      chat_socket     = 0;

static char     recv_buf[1024];
static int      recv_buf_pos    = 0;

int WINAPI compat_recv(SOCKET s, char *buf, int len, int flags)
{
    int ret = recv(s, buf, len, flags);

    dprintf("recv(s=%d, buf=%p, len=%d, flags=%08X) -> %d (err: %d)\n", s, buf, len, flags, ret, WSAGetLastError());

    if (s == chat_socket && ret > 0)
    {
        if (recv_buf_pos + ret < sizeof recv_buf)
        {
            memcpy(recv_buf + recv_buf_pos, buf, ret);
            recv_buf_pos += ret;
            recv_buf[recv_buf_pos] = '\0';

            char *l = recv_buf;

            for (int i = 0; i < recv_buf_pos; i++)
            {
                if (recv_buf[i] == '\n')
                {
                    recv_buf[i] = '\0';

                    if (strncmp(l, "TUNNEL", 6) == 0)
                    {
                        dprintf("Message: %s\n", l);

                        char *last = strrchr(l, ' ');
                        if (last && last - recv_buf > 1)
                        {
                            last++;
                            tunnel_ip = inet_addr(last);
                            if (tunnel_ip == 0xFFFFFFFF)
                                tunnel_ip = 0;

                            dprintf("Tunnel ip set to %s\n", inet_ntoa(*(struct in_addr *)&tunnel_ip));
                        }
                    }

                    l = recv_buf + i + 1;
                }
            }

            recv_buf_pos = 0;

            if (l < recv_buf + sizeof recv_buf)
            {
                int len = strlen(l);
                memmove(recv_buf, l, len);
                recv_buf[len] = '\0';
                recv_buf_pos = len;
            }
        }
        else
        {
            dprintf("  receive buffer full! discarding it\n");
            recv_buf_pos = 0;
        }
    }

    return ret;
}

int WINAPI compat_closesocket(SOCKET s)
{
    if (s == chat_socket)
    {
        dprintf("Chat socket closed.\n");
        chat_socket = 0;
        recv_buf_pos = 0;
    }

    return closesocket(s);
}

int WINAPI compat_send(SOCKET s, const char *buf, int len, int flags)
{
    dprintf("send(s=%d, buf=%p, len=%d, flags=%08X)\n", s, buf, len, flags);

    // detect chat socket
    if (chat_socket == 0 && len >= 5 && strncmp(buf, "CVERS", 5) == 0)
    {
        const char *ann = "TUNNEL 1\r\n";
        chat_socket = s;
        dprintf("Chat socket found.\n");
        send(s, ann, strlen(ann), 0);
    }

    return send(s, buf, len, flags);
}

int WINAPI compat_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr_in *from, int *fromlen)
{
    int ret = recvfrom(s, buf, len, flags, (struct sockaddr *)from, fromlen);

    dprintf("recvfrom(s=%d, buf=%p, len=%d, flags=%08X, from=%p, fromlen=%p (%d) -> %d (err: %d)\n", s, buf, len, flags, from, fromlen, *fromlen, ret, WSAGetLastError());

    if (from->sin_addr.s_addr == tunnel_ip)
    {
        from->sin_addr.s_addr = ntohs(from->sin_port);
        // hack: assuming single port
        from->sin_port = port;
        dprintf("  receiving from %s:%d\n", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }

    return ret;
}

int WINAPI compat_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr_in *to, int tolen)
{
    struct sockaddr_in tunnel_to;

    dprintf("sendto(s=%d, buf=%p, len=%d, flags=%08X, to=%p, tolen=%d)\n", s, buf, len, flags, to, tolen);

    if ((to->sin_addr.s_addr >> 16) == 0 && tunnel_ip)
    {
        dprintf("  forwarding to %s:%d\n", inet_ntoa(*(struct in_addr *)&tunnel_ip), (int)to->sin_addr.s_addr);
        // hack: assuming single port
        port = to->sin_port;

        tunnel_to.sin_family = AF_INET;
        tunnel_to.sin_port = htons(to->sin_addr.s_addr);
        tunnel_to.sin_addr.s_addr = tunnel_ip;
        to = &tunnel_to;
    }

    return sendto(s, buf, len, flags, (struct sockaddr *)to, tolen);
}

