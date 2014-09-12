/*
 * Copyright © 2013 Ran Benita <ran234@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef COMPOSE_COMPOSE_H
#define COMPOSE_COMPOSE_H

#include "xkbcommon/xkbcommon-compose.h"
#include "utils.h"
#include "context.h"

/*
 * The compose table data structure is a simple trie.  An example will
 * help.  Given these sequences:
 *
 *      <A> <B>        : "first"  dead_a
 *      <A> <C> <D>    : "second" dead_b
 *      <E> <F>        : "third"  dead_c
 *
 * the trie would look like:
 *
 * [root] ---> [<A>] -----------------> [<E>] -#
 *   |           |                        |
 *   #           v                        v
 *             [<B>] ---> [<C>] -#      [<F>] -#
 *               |          |             -
 *               #          v             #
 *                        [<D>] -#
 *                          |
 *                          #
 * where:
 * - [root] is a special empty root node.
 * - [<X>] is a node for a sequence keysym <X>.
 * - right arrows are `next` pointers.
 * - down arrows are `successor` pointers.
 * - # is a nil pointer.
 *
 * The nodes are all kept in a contiguous array.  Pointers are represented
 * as integer offsets into this array.  A nil pointer is represented as 0
 * (which, helpfully, is the offset of the empty root node).
 *
 * Nodes without a successor are leaf nodes.  Since a sequence cannot be a
 * prefix of another, these are exactly the nodes which terminate the
 * sequences (in a bijective manner).
 *
 * A leaf contains the result data of its sequence.  The result keysym is
 * contained in the node struct itself; the result UTF-8 string is a byte
 * offset into an array of the form "\0first\0second\0third" (the initial
 * \0 is so offset 0 points to an empty string).
 */

struct compose_node {
    xkb_keysym_t keysym;
    /* Offset into xkb_compose_table::nodes.  But see below. */
    uint32_t next_;

    union {
        /* Offset into xkb_compose_table::nodes. */
        uint32_t successor;
        struct {
            /* Offset into xkb_compose_table::utf8. */
            uint32_t utf8;
            xkb_keysym_t keysym;
        } leaf;
    } u;
};

/*
 * Save some bytes in compose_node by encoding the union tag in the last
 * bit of the `next` field.
 */
#define COMPOSE_NODE_IS_LEAF_FLAG (1u << 31)
static inline void
compose_node_set_next(struct compose_node *node, uint32_t next) {
    node->next_ = next | (node->next_ & COMPOSE_NODE_IS_LEAF_FLAG);
}
static inline uint32_t
compose_node_next(const struct compose_node *node) {
    return node->next_ & ~COMPOSE_NODE_IS_LEAF_FLAG;
}
static inline void
compose_node_set_is_leaf(struct compose_node *node, bool is_leaf) {
    node->next_ = (compose_node_next(node) |
                   (is_leaf ? COMPOSE_NODE_IS_LEAF_FLAG : 0));
}
static inline bool
compose_node_is_leaf(const struct compose_node *node) {
    return node->next_ & COMPOSE_NODE_IS_LEAF_FLAG;
}

struct xkb_compose_table {
    int refcnt;
    enum xkb_compose_format format;
    enum xkb_compose_compile_flags flags;
    struct xkb_context *ctx;

    char *locale;

    darray_char utf8;
    darray(struct compose_node) nodes;
};

#endif
