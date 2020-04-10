#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_live.h"
#include "ngx_live_segment_index.h"
#include "ngx_live_timeline.h"


#define NGX_LIVE_SEGMENT_INDEX_FREE_CYCLES  (5)


enum {
    NGX_LIVE_BP_SEGMENT_INDEX,

    NGX_LIVE_BP_COUNT
};


typedef struct {
    ngx_uint_t                  force_memory_segments;
} ngx_live_segment_index_preset_conf_t;


typedef enum {
    ngx_live_segment_persist_none,
    ngx_live_segment_persist_ok,
    ngx_live_segment_persist_error,
} ngx_live_segment_persist_e;

struct ngx_live_segment_index_s {
    ngx_rbtree_node_t           node;       /* key = segment_index */
    ngx_queue_t                 queue;
    ngx_queue_t                 cleanup;
    ngx_live_segment_persist_e  persist;
    unsigned                    free:1;
};


typedef struct {
    ngx_block_pool_t           *block_pool;
    ngx_queue_t                 queue;
    ngx_rbtree_t                rbtree;
    ngx_rbtree_node_t           sentinel;
    unsigned                    no_free:1;
} ngx_live_segment_index_channel_ctx_t;


static void *ngx_live_segment_index_create_preset_conf(ngx_conf_t *cf);
static char *ngx_live_segment_index_merge_preset_conf(ngx_conf_t *cf,
    void *parent, void *child);

static ngx_int_t ngx_live_segment_index_postconfiguration(ngx_conf_t *cf);


static ngx_live_module_t  ngx_live_segment_index_module_ctx = {
    NULL,                                     /* preconfiguration */
    ngx_live_segment_index_postconfiguration, /* postconfiguration */

    NULL,                                     /* create main configuration */
    NULL,                                     /* init main configuration */

    ngx_live_segment_index_create_preset_conf,/* create preset configuration */
    ngx_live_segment_index_merge_preset_conf, /* merge preset configuration */
};


static ngx_command_t  ngx_live_segment_index_commands[] = {
    { ngx_string("force_memory_segments"),
      NGX_LIVE_MAIN_CONF|NGX_LIVE_PRESET_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_LIVE_PRESET_CONF_OFFSET,
      offsetof(ngx_live_segment_index_preset_conf_t, force_memory_segments),
      NULL },

      ngx_null_command
};

ngx_module_t  ngx_live_segment_index_module = {
    NGX_MODULE_V1,
    &ngx_live_segment_index_module_ctx,       /* module context */
    ngx_live_segment_index_commands,          /* module directives */
    NGX_LIVE_MODULE,                          /* module type */
    NULL,                                     /* init master */
    NULL,                                     /* init module */
    NULL,                                     /* init process */
    NULL,                                     /* init thread */
    NULL,                                     /* exit thread */
    NULL,                                     /* exit process */
    NULL,                                     /* exit master */
    NGX_MODULE_V1_PADDING
};


static void
ngx_live_segment_index_free(ngx_live_channel_t *channel,
    ngx_live_segment_index_t *index, uint32_t *truncate)
{
    uint32_t                               segment_index;
    ngx_live_segment_index_channel_ctx_t  *cctx;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);
    if (cctx->no_free) {
        return;
    }

    if (!index->free) {
        segment_index = index->node.key;

        ngx_live_segment_cache_free_by_index(channel, segment_index);
        ngx_rbtree_delete(&cctx->rbtree, &index->node);

        index->node.key = segment_index;    /* ngx_rbtree_delete zeroes key */
        index->free = 1;

        if (index->persist != ngx_live_segment_persist_ok) {
            *truncate = segment_index;
        }
    }

    if (!ngx_queue_empty(&index->cleanup)) {
        return;
    }

    ngx_queue_remove(&index->queue);
    ngx_block_pool_free(cctx->block_pool, NGX_LIVE_BP_SEGMENT_INDEX, index);
}

static void
ngx_live_segment_index_free_non_forced(ngx_live_channel_t *channel)
{
    uint32_t                               cycles;
    uint32_t                               truncate;
    ngx_queue_t                           *q;
    ngx_live_segment_index_t              *index;
    ngx_live_segment_index_channel_ctx_t  *cctx;
    ngx_live_segment_index_preset_conf_t  *spcf;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);

    spcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_index_module);

    /* Note: if many segments are not persisted (dvr is off or slow)
        limit the number of freed segments */
    cycles = NGX_LIVE_SEGMENT_INDEX_FREE_CYCLES;
    truncate = 0;

    q = ngx_queue_head(&cctx->queue);
    while (q != ngx_queue_sentinel(&cctx->queue)) {

        index = ngx_queue_data(q, ngx_live_segment_index_t, queue);

        if (index->node.key + spcf->force_memory_segments >=
            channel->next_segment_index)
        {
            break;
        }

        q = ngx_queue_next(q);      /* move to next before freeing */

        if (index->persist != ngx_live_segment_persist_none) {
            ngx_live_segment_index_free(channel, index, &truncate);
        }

        cycles--;
        if (cycles <= 0) {
            break;
        }
    }

    if (truncate) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_index_free_non_forced: "
            "truncating timelines, index: %uD", truncate);

        ngx_live_timelines_truncate(channel, truncate);
    }
}

ngx_int_t
ngx_live_segment_index_create(ngx_live_channel_t *channel)
{
    ngx_live_segment_index_t              *index;
    ngx_live_segment_index_channel_ctx_t  *cctx;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);

    index = ngx_block_pool_calloc(cctx->block_pool, NGX_LIVE_BP_SEGMENT_INDEX);
    if (index == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_index_create: alloc failed");
        return NGX_ERROR;
    }

    index->node.key = channel->next_segment_index;
    ngx_queue_init(&index->cleanup);

    ngx_rbtree_insert(&cctx->rbtree, &index->node);
    ngx_queue_insert_tail(&cctx->queue, &index->queue);

    ngx_live_segment_index_free_non_forced(channel);

    return NGX_OK;
}

ngx_live_segment_index_t *
ngx_live_segment_index_get(ngx_live_channel_t *channel, uint32_t segment_index)
{
    ngx_rbtree_t                          *rbtree;
    ngx_rbtree_node_t                     *node, *sentinel;
    ngx_live_segment_index_channel_ctx_t  *cctx;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);

    rbtree = &cctx->rbtree;

    node = rbtree->root;
    sentinel = rbtree->sentinel;

    while (node != sentinel) {

        if (segment_index < node->key) {
            node = node->left;
            continue;
        }

        if (segment_index > node->key) {
            node = node->right;
            continue;
        }

        return (ngx_live_segment_index_t *) node;
    }

    return NULL;
}

void
ngx_live_segment_index_persisted(ngx_live_channel_t *channel,
    uint32_t min_segment_index, uint32_t max_segment_index, ngx_int_t rc)
{
    uint32_t                               truncate;
    uint32_t                               segment_index;
    ngx_queue_t                           *q;
    ngx_live_segment_index_t              *index;
    ngx_live_segment_persist_e             persist;
    ngx_live_segment_index_channel_ctx_t  *cctx;
    ngx_live_segment_index_preset_conf_t  *spcf;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);

    spcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_index_module);

    for (segment_index = min_segment_index; ; segment_index++) {

        if (segment_index >= max_segment_index) {
            return;
        }

        index = ngx_live_segment_index_get(channel, segment_index);
        if (index != NULL) {
            /* Note: may not find the index when a dvr bucket is saved and
                some segment indexes contained in it did not exist */
            break;
        }
    }

    q = &index->queue;
    persist = rc == NGX_OK ? ngx_live_segment_persist_ok :
        ngx_live_segment_persist_error;
    truncate = 0;

    for ( ;; ) {

        if (index->persist != ngx_live_segment_persist_none) {
            ngx_log_error(NGX_LOG_ALERT, &channel->log, 0,
                "ngx_live_segment_index_persisted: "
                "already called for segment %ui", index->node.key);
        }

        index->persist = persist;

        q = ngx_queue_next(q);      /* move to next before freeing */

        if (index->node.key + spcf->force_memory_segments <
            channel->next_segment_index)
        {
            ngx_live_segment_index_free(channel, index, &truncate);
        }

        if (q == ngx_queue_sentinel(&cctx->queue)) {
            break;
        }

        index = ngx_queue_data(q, ngx_live_segment_index_t, queue);
        if (index->node.key >= max_segment_index) {
            break;
        }
    }

    if (truncate) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_index_persisted: "
            "truncating timelines, index: %uD", truncate);

        ngx_live_timelines_truncate(channel, truncate);
    }
}

static void
ngx_live_segment_index_cleanup(ngx_live_segment_index_t *index)
{
    ngx_queue_t                 *q;
    ngx_live_segment_cleanup_t  *cln;

    q = ngx_queue_head(&index->cleanup);
    while (q != ngx_queue_sentinel(&index->cleanup)) {

        cln = ngx_queue_data(q, ngx_live_segment_cleanup_t, queue);

        q = ngx_queue_next(q);      /* move to next before freeing */

        if (cln->handler) {
            cln->handler(cln->data);
        }
    }
}

static ngx_int_t
ngx_live_segment_index_watermark(ngx_live_channel_t *channel, void *ectx)
{
    uint32_t                               truncate;
    ngx_uint_t                             level;
    ngx_queue_t                           *q;
    ngx_live_segment_index_t              *index;
    ngx_live_segment_index_channel_ctx_t  *cctx;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);

    truncate = 0;
    level = NGX_LOG_NOTICE;

    q = ngx_queue_head(&cctx->queue);
    while (q != ngx_queue_sentinel(&cctx->queue)) {

        if (channel->mem_left >= channel->mem_low_watermark) {
            break;
        }

        index = ngx_queue_data(q, ngx_live_segment_index_t, queue);

        q = ngx_queue_next(q);      /* move to next before freeing */

        /* Note: need to disable free, since cleanup handlers call 'persisted'
            which in turn calls 'free', and may release index/q */
        cctx->no_free = 1;
        ngx_live_segment_index_cleanup(index);
        cctx->no_free = 0;

        if (!ngx_queue_empty(&index->cleanup)) {
            ngx_log_error(NGX_LOG_ALERT, &channel->log, 0,
                "ngx_live_segment_index_watermark: cleanup queue not empty");
            break;
        }

        if (!index->free && index->persist == ngx_live_segment_persist_none) {
            level = NGX_LOG_ERR;
        }

        ngx_live_segment_index_free(channel, index, &truncate);
    }

    if (truncate) {
        ngx_log_error(level, &channel->log, 0,
            "ngx_live_segment_index_watermark: "
            "truncating timelines, index: %uD", truncate);

        ngx_live_timelines_truncate(channel, truncate);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_index_segment_free(ngx_live_channel_t *channel, void *ectx)
{
    uint32_t                               ignore;
    uint32_t                               min_segment_index;
    ngx_queue_t                           *q;
    ngx_live_segment_index_t              *index;
    ngx_live_segment_index_channel_ctx_t  *cctx;

    min_segment_index = (uintptr_t) ectx;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);

    q = ngx_queue_head(&cctx->queue);
    while (q != ngx_queue_sentinel(&cctx->queue)) {

        index = ngx_queue_data(q, ngx_live_segment_index_t, queue);

        if (index->node.key >= min_segment_index) {
            break;
        }

        q = ngx_queue_next(q);      /* move to next before freeing */

        ngx_live_segment_index_free(channel, index, &ignore);
    }

    return NGX_OK;
}

ngx_int_t
ngx_live_segment_index_lock(ngx_live_segment_cleanup_t *cln,
    ngx_live_segment_t *segment)
{
    ngx_live_input_bufs_lock_t  *lock;

    lock = ngx_live_input_bufs_lock(segment->track, segment->node.key,
        segment->data_head->data);
    if (lock == NULL) {
        return NGX_ERROR;
    }

    *cln->locks_end++ = lock;

    return NGX_OK;
}

static void
ngx_live_segment_index_unlock(void *data)
{
    ngx_live_segment_cleanup_t   *cln = data;
    ngx_live_input_bufs_lock_t  **cur;

    for (cur = cln->locks; cur < cln->locks_end; cur++) {
        ngx_live_input_bufs_unlock(*cur);
    }

    ngx_queue_remove(&cln->queue);
}

ngx_live_segment_cleanup_t *
ngx_live_segment_index_cleanup_add(ngx_pool_t *pool,
    ngx_live_segment_index_t *index, uint32_t max_locks)
{
    ngx_pool_cleanup_t           *cln;
    ngx_live_segment_cleanup_t   *result;
    ngx_live_input_bufs_lock_t  **locks;

    cln = ngx_pool_cleanup_add(pool, sizeof(*result) +
        sizeof(*locks) * max_locks);
    if (cln == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, pool->log, 0,
            "ngx_live_segment_index_cleanup_add: failed to add cleanup item");
        return NULL;
    }

    result = cln->data;
    locks = (void *) (result + 1);

    result->handler = NULL;
    result->data = NULL;
    result->locks = locks;
    result->locks_end = locks;

    cln->handler = ngx_live_segment_index_unlock;
    cln->data = result;

    ngx_queue_insert_tail(&index->cleanup, &result->queue);

    return result;
}

static ngx_int_t
ngx_live_segment_index_channel_init(ngx_live_channel_t *channel, void *ectx)
{
    size_t                                 block_sizes[NGX_LIVE_BP_COUNT];
    ngx_live_segment_index_channel_ctx_t  *cctx;

    cctx = ngx_pcalloc(channel->pool, sizeof(*cctx));
    if (cctx == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_index_channel_init: alloc failed");
        return NGX_ERROR;
    }

    block_sizes[NGX_LIVE_BP_SEGMENT_INDEX] = sizeof(ngx_live_segment_index_t);

    cctx->block_pool = ngx_live_channel_create_block_pool(channel, block_sizes,
        NGX_LIVE_BP_COUNT);
    if (cctx->block_pool == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_index_channel_init: create block pool failed");
        return NGX_ERROR;
    }

    ngx_rbtree_init(&cctx->rbtree, &cctx->sentinel, ngx_rbtree_insert_value);
    ngx_queue_init(&cctx->queue);

    ngx_live_set_ctx(channel, cctx, ngx_live_segment_index_module);

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_index_channel_free(ngx_live_channel_t *channel, void *ectx)
{
    ngx_queue_t                           *q;
    ngx_live_segment_index_t              *index;
    ngx_live_segment_index_channel_ctx_t  *cctx;

    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_index_module);
    if (cctx == NULL) {
        return NGX_OK;
    }

    /* Note: should not try to free segments when the channel is freed since
        the segment cache module may have already freed everything */
    cctx->no_free = 1;

    q = ngx_queue_head(&cctx->queue);
    while (q != ngx_queue_sentinel(&cctx->queue)) {

        index = ngx_queue_data(q, ngx_live_segment_index_t, queue);

        q = ngx_queue_next(q);      /* move to next before freeing */

        ngx_live_segment_index_cleanup(index);

        if (!ngx_queue_empty(&index->cleanup)) {
            ngx_log_error(NGX_LOG_ALERT, &channel->log, 0,
                "ngx_live_segment_index_channel_free: "
                "cleanup queue not empty");
        }
    }

    return NGX_OK;
}

static void *
ngx_live_segment_index_create_preset_conf(ngx_conf_t *cf)
{
    ngx_live_segment_index_preset_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_live_segment_index_preset_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->force_memory_segments = NGX_CONF_UNSET_UINT;

    return conf;
}

static char *
ngx_live_segment_index_merge_preset_conf(ngx_conf_t *cf, void *parent,
    void *child)
{
    ngx_live_segment_index_preset_conf_t  *prev = parent;
    ngx_live_segment_index_preset_conf_t  *conf = child;

    ngx_conf_merge_uint_value(conf->force_memory_segments,
                              prev->force_memory_segments, 5);

    return NGX_CONF_OK;
}


static ngx_live_channel_event_t  ngx_live_segment_index_channel_events[] = {
    { ngx_live_segment_index_channel_init, NGX_LIVE_EVENT_CHANNEL_INIT },
    { ngx_live_segment_index_channel_free, NGX_LIVE_EVENT_CHANNEL_FREE },
    { ngx_live_segment_index_segment_free,
        NGX_LIVE_EVENT_CHANNEL_SEGMENT_FREE },
    { ngx_live_segment_index_watermark, NGX_LIVE_EVENT_CHANNEL_WATERMARK },
      ngx_live_null_event
};

static ngx_int_t
ngx_live_segment_index_postconfiguration(ngx_conf_t *cf)
{
    if (ngx_live_core_channel_events_add(cf,
        ngx_live_segment_index_channel_events) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}
