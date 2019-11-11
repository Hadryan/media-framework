#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_live_segment_list.h"


/* sizeof ngx_live_segment_list_node_t = 512 */
#define SEGMENT_LIST_NODE_ELEMENTS  (55)


enum {
    NGX_LIVE_BP_SEGMENT_LIST_NODE,

    NGX_LIVE_BP_COUNT
};


struct ngx_live_segment_list_node_s {
    ngx_rbtree_node_t           node;        /* key = segment_index */
    ngx_queue_t                 queue;
    int64_t                     time;
    ngx_uint_t                  nelts;
    ngx_live_segment_repeat_t   elts[SEGMENT_LIST_NODE_ELEMENTS];
};


ngx_int_t
ngx_live_segment_list_init(ngx_live_channel_t *channel,
    ngx_live_segment_list_t *segment_list)
{
    size_t  block_sizes[NGX_LIVE_BP_COUNT];

    ngx_rbtree_init(&segment_list->rbtree, &segment_list->sentinel,
        ngx_rbtree_insert_value);
    ngx_queue_init(&segment_list->queue);

    block_sizes[NGX_LIVE_BP_SEGMENT_LIST_NODE] =
        sizeof(ngx_live_segment_list_node_t);

    segment_list->block_pool = ngx_live_channel_create_block_pool(channel,
        block_sizes, NGX_LIVE_BP_COUNT);
    if (segment_list->block_pool == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_list_init: create pool failed");
        return NGX_ERROR;
    }

    segment_list->log = &channel->log;

    /* other fields are assumed to be initialized to 0 */

    return NGX_OK;
}

ngx_int_t
ngx_live_segment_list_add(ngx_live_segment_list_t *segment_list,
    uint32_t segment_index, int64_t time, uint32_t duration)
{
    ngx_live_segment_repeat_t     *last_elt;
    ngx_live_segment_list_node_t  *last;

    if (!ngx_queue_empty(&segment_list->queue)) {

        last = ngx_queue_data(ngx_queue_last(&segment_list->queue),
            ngx_live_segment_list_node_t, queue);

        if (segment_list->last_time == time &&
            segment_index == segment_list->last_segment_index + 1)
        {
            last_elt = &last->elts[last->nelts - 1];
            if (last_elt->duration == duration) {
                last_elt->repeat_count++;
                goto add;
            }

            if (last->nelts < SEGMENT_LIST_NODE_ELEMENTS) {

                last_elt = &last->elts[last->nelts];
                last->nelts++;

                last_elt->duration = duration;
                last_elt->repeat_count = 1;
                goto add;
            }
        }
    }

    last = ngx_block_pool_alloc(segment_list->block_pool,
        NGX_LIVE_BP_SEGMENT_LIST_NODE);
    if (last == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, segment_list->log, 0,
            "ngx_live_segment_list_add: alloc failed");
        return NGX_ERROR;
    }

    last->time = time;
    last->nelts = 1;
    last->elts[0].duration = duration;
    last->elts[0].repeat_count = 1;
    last->node.key = segment_index;

    ngx_queue_insert_tail(&segment_list->queue, &last->queue);
    ngx_rbtree_insert(&segment_list->rbtree, &last->node);

add:

    segment_list->last_segment_index = segment_index;
    segment_list->last_time = time + duration;

    return NGX_OK;
}

void
ngx_live_segment_list_free_nodes(ngx_live_segment_list_t *segment_list,
    uint32_t min_used_segment_index)
{
    ngx_queue_t                   *next;
    ngx_queue_t                   *queue;
    ngx_live_segment_list_node_t  *node;
    ngx_live_segment_list_node_t  *next_node;

    if (ngx_queue_empty(&segment_list->queue)) {
        return;
    }

    for (queue = ngx_queue_head(&segment_list->queue); ; queue = next) {

        next = ngx_queue_next(queue);
        if (next == ngx_queue_sentinel(&segment_list->queue)) {
            break;
        }

        next_node = ngx_queue_data(next, ngx_live_segment_list_node_t, queue);

        /* Note: when next_node->node.key == min_used_segment_index the
            the current node doesn't have any used segments, but there may
            still be iterators pointing to it, so it should not be freed */

        if (next_node->node.key >= min_used_segment_index) {
            break;
        }

        node = ngx_queue_data(queue, ngx_live_segment_list_node_t, queue);
        ngx_queue_remove(&node->queue);
        ngx_rbtree_delete(&segment_list->rbtree, &node->node);

        ngx_block_pool_free(segment_list->block_pool,
            NGX_LIVE_BP_SEGMENT_LIST_NODE, node);
    }
}

ngx_int_t
ngx_live_segment_list_get_segment_time(ngx_live_segment_list_t *segment_list,
    uint32_t segment_index, int64_t *result)
{
    int64_t                        time;
    ngx_queue_t                   *prev;
    ngx_rbtree_t                  *rbtree;
    ngx_rbtree_node_t             *rbnode;
    ngx_rbtree_node_t             *sentinel;
    ngx_rbtree_node_t             *next_node;
    ngx_live_segment_repeat_t     *elt;
    ngx_live_segment_repeat_t     *last;
    ngx_live_segment_list_node_t  *node;

    rbtree = &segment_list->rbtree;
    rbnode = rbtree->root;
    sentinel = rbtree->sentinel;

    if (rbnode == sentinel) {
        return NGX_ERROR;
    }

    for ( ;; ) {

        next_node = (segment_index < rbnode->key) ? rbnode->left :
            rbnode->right;
        if (next_node == sentinel) {
            break;
        }

        rbnode = next_node;
    }

    node = (ngx_live_segment_list_node_t*) rbnode;
    if (segment_index < node->node.key)
    {
        /* Note: since we don't know the end index of each node, it is possible
            that we made a wrong right turn, in that case, we need to go back
            one node */
        prev = ngx_queue_prev(&node->queue);
        if (prev == ngx_queue_sentinel(&segment_list->queue)) {
            return NGX_ERROR;
        }

        node = ngx_queue_data(prev, ngx_live_segment_list_node_t, queue);
    }

    segment_index -= node->node.key;
    time = node->time;

    for (elt = node->elts, last = elt + node->nelts; elt < last; elt++) {

        if (segment_index <= elt->repeat_count) {
            *result = time + (int64_t) segment_index * elt->duration;
            return NGX_OK;
        }

        segment_index -= elt->repeat_count;
        time += (int64_t) elt->repeat_count * elt->duration;
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_live_segment_list_get_closest_segment(
    ngx_live_segment_list_t *segment_list, int64_t time,
    uint32_t *segment_index, int64_t *segment_time,
    ngx_live_segment_iterator_t *iterator)
{
    int64_t                        elt_duration;
    ngx_queue_t                   *prev;
    ngx_rbtree_t                  *rbtree;
    ngx_rbtree_node_t             *rbnode;
    ngx_rbtree_node_t             *sentinel;
    ngx_rbtree_node_t             *next_node;
    ngx_live_segment_repeat_t     *elt;
    ngx_live_segment_repeat_t     *last;
    ngx_live_segment_list_node_t  *node;

    rbtree = &segment_list->rbtree;
    rbnode = rbtree->root;
    sentinel = rbtree->sentinel;

    if (rbnode == sentinel) {
        return NGX_ERROR;
    }

    for (;; ) {

        node = (ngx_live_segment_list_node_t*) rbnode;
        next_node = (time < node->time) ? rbnode->left : rbnode->right;
        if (next_node == sentinel) {
            break;
        }

        rbnode = next_node;
    }

    if (time < node->time)
    {
        /* Note: since we don't know the end index of each node, it is possible
            that we made a wrong right turn, in that case, we need to go back
            one node */
        prev = ngx_queue_prev(&node->queue);
        if (prev == ngx_queue_sentinel(&segment_list->queue)) {
            return NGX_ERROR;
        }

        node = ngx_queue_data(prev, ngx_live_segment_list_node_t, queue);
    }

    *segment_index = node->node.key;
    *segment_time = node->time;

    for (elt = node->elts, last = elt + node->nelts; elt < last; elt++) {

        elt_duration = (int64_t) elt->repeat_count * elt->duration;

        if (time >= *segment_time + elt_duration) {
            *segment_index += elt->repeat_count;
            *segment_time += elt_duration;
            continue;
        }

        iterator->node = node;
        iterator->elt = elt;
        iterator->offset = (time - *segment_time + elt->duration / 2) /
            elt->duration;

        *segment_index += iterator->offset;
        *segment_time += (int64_t) iterator->offset * elt->duration;

        return NGX_OK;
    }

    return NGX_ERROR;
}


size_t
ngx_live_segment_list_json_get_size(ngx_live_segment_list_t *segment_list)
{
    size_t  result;

    result = sizeof("{}") - 1;

    if (!ngx_queue_empty(&segment_list->queue)) {
        result += sizeof("\"min_index\":") - 1 + NGX_INT32_LEN +
            sizeof(",\"max_index\":") - 1 + NGX_INT32_LEN;
    }

    return result;
}

u_char *
ngx_live_segment_list_json_write(u_char *p,
    ngx_live_segment_list_t *segment_list)
{
    ngx_queue_t                   *q;
    ngx_live_segment_list_node_t  *first;

    *p++ = '{';

    if (!ngx_queue_empty(&segment_list->queue)) {

        q = ngx_queue_head(&segment_list->queue);
        first = ngx_queue_data(q, ngx_live_segment_list_node_t, queue);

        p = ngx_copy_fix(p, "\"min_index\":");
        p = ngx_sprintf(p, "%uD", (uint32_t) first->node.key);

        p = ngx_copy_fix(p, ",\"max_index\":");
        p = ngx_sprintf(p, "%uD", segment_list->last_segment_index);
    }

    *p++ = '}';

    return p;
}


void
ngx_live_segment_iterator_last(ngx_live_segment_list_t *segment_list,
    ngx_live_segment_iterator_t *iterator)
{
    ngx_queue_t                   *q;
    ngx_live_segment_list_node_t  *last;

    /* Note: must not be called when empty */
    q = ngx_queue_last(&segment_list->queue);
    last = ngx_queue_data(q, ngx_live_segment_list_node_t, queue);
    iterator->node = last;
    iterator->elt = &last->elts[last->nelts - 1];
    iterator->offset = iterator->elt->repeat_count - 1;
}

static void
ngx_live_segment_iterator_move_next(ngx_live_segment_iterator_t *iterator)
{
    ngx_queue_t  *next;

    if (iterator->offset < iterator->elt->repeat_count) {
        return;
    }

    iterator->elt++;
    iterator->offset = 0;

    if (iterator->elt < iterator->node->elts + iterator->node->nelts) {
        return;
    }

    next = ngx_queue_next(&iterator->node->queue);
    iterator->node = ngx_queue_data(next, ngx_live_segment_list_node_t, queue);
    iterator->elt = iterator->node->elts;
}

void
ngx_live_segment_iterator_get_one(ngx_live_segment_iterator_t *iterator,
    uint32_t *duration)
{
    ngx_live_segment_iterator_move_next(iterator);

    *duration = iterator->elt->duration;
    iterator->offset++;
}

void
ngx_live_segment_iterator_get_element(ngx_live_segment_iterator_t *iterator,
    ngx_live_segment_repeat_t *segment_duration)
{
    ngx_live_segment_repeat_t  *elt;

    ngx_live_segment_iterator_move_next(iterator);

    elt = iterator->elt;
    segment_duration->duration = elt->duration;
    segment_duration->repeat_count = elt->repeat_count - iterator->offset;
    iterator->offset = elt->repeat_count;
}
