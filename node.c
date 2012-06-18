/*
 * Copyright (c) 2011, 2012 Toni Spets <toni.spets@iki.fi>
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

#include "list.h"
#include "node.h"

static struct node *nodes;

struct node *node_new(const char *mac, const struct sockaddr_in *in)
{
    struct node *cur = LIST_NEW(*nodes);
    memcpy(cur->mac, mac, sizeof cur->mac);
    memcpy(&cur->in, in, sizeof cur->in);
    return cur;
}

void node_insert(struct node *n)
{
    LIST_INSERT(nodes, n);
}

struct node *node_from_mac(const char *mac)
{
    struct node *cur;

    LIST_FOREACH (nodes, cur)
    {
        if (memcmp(cur->mac, mac, 6) == 0)
            return cur;
    }

    return NULL;
}

struct node *node_from_ip(const struct sockaddr_in *in)
{
    struct node *cur;

    LIST_FOREACH (nodes, cur)
    {
        if (cur->in.sin_addr.s_addr == in->sin_addr.s_addr
                && cur->in.sin_port == in->sin_port)
            return cur;
    }

    return NULL;
}

void node_free()
{
    LIST_FREE(nodes);
}
