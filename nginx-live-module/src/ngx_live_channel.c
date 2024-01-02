#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_live.h"
#include "ngx_live_media_info.h"
#include "ngx_live_segment_info.h"
#include "ngx_live_timeline.h"


typedef struct {
    ngx_rbtree_t            rbtree;
    ngx_rbtree_node_t       sentinel;
    ngx_queue_t             queue;
} ngx_live_channels_t;


static ngx_live_channels_t  ngx_live_channels;


static size_t ngx_live_variant_json_track_ids_get_size(
    ngx_live_variant_t *obj);

static u_char *ngx_live_variant_json_track_ids_write(u_char *p,
    ngx_live_variant_t *obj);

/* must match ngx_live_track_type_e */
ngx_str_t  ngx_live_track_type_names[] = {
    ngx_string("default"),
    ngx_string("filler"),
    ngx_null_string
};


/* must match ngx_ksmp_variant_role_e */
ngx_str_t  ngx_live_variant_role_names[] = {
    ngx_string("main"),
    ngx_string("alternate"),
    ngx_null_string
};


/* must match KMP_MEDIA_XXX */
ngx_str_t  ngx_live_track_media_type_names[] = {
    ngx_string("video"),
    ngx_string("audio"),
    ngx_string("subtitle"),
    ngx_null_string
};


#include "ngx_live_channel_json.h"


static void ngx_live_track_channel_free(ngx_live_track_t *track,
    ngx_uint_t event);


ngx_int_t
ngx_live_channel_init_process(ngx_cycle_t *cycle)
{
    ngx_rbtree_init(&ngx_live_channels.rbtree, &ngx_live_channels.sentinel,
        ngx_str_rbtree_insert_value);
    ngx_queue_init(&ngx_live_channels.queue);
    return NGX_OK;
}


/* channel */

ngx_live_channel_t *
ngx_live_channel_get(ngx_str_t *id)
{
    uint32_t  hash;

    hash = ngx_crc32_short(id->data, id->len);
    return (ngx_live_channel_t *) ngx_str_rbtree_lookup(
        &ngx_live_channels.rbtree, id, hash);
}


static u_char *
ngx_live_channel_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    u_char              *p;
    ngx_live_channel_t  *channel;

    p = buf;

    channel = log->data;

    if (channel != NULL) {
        p = ngx_snprintf(buf, len, ", nsi: %uD, channel: %V",
            channel->next_segment_index, &channel->sn.str);
    }

    return p;
}


static void
ngx_live_random_bytes(ngx_log_t *log, void *dst, size_t len)
{
    u_char  *p = dst;

#if (NGX_OPENSSL)

    if (RAND_bytes(p, len) == 1) {
        return;
    }

    ngx_ssl_error(NGX_LOG_ERR, log, 0, "RAND_bytes() failed");

#endif

    for (; len > 0; len--) {
        *p++ = ngx_random() & 0xff;
    }
}


ngx_int_t
ngx_live_channel_create(ngx_str_t *id, ngx_live_conf_ctx_t *conf_ctx,
    ngx_pool_t *temp_pool, ngx_live_channel_t **result)
{
    uint32_t                      hash;
    ngx_int_t                     rc;
    ngx_pool_t                   *pool;
    ngx_live_channel_t           *channel;
    ngx_live_core_preset_conf_t  *cpcf;

    if (id->len > KMP_MAX_CHANNEL_ID_LEN) {
        ngx_log_error(NGX_LOG_ERR, temp_pool->log, 0,
            "ngx_live_channel_create: channel id \"%V\" too long", id);
        return NGX_INVALID_ARG;
    }

    hash = ngx_crc32_short(id->data, id->len);
    channel = (ngx_live_channel_t *) ngx_str_rbtree_lookup(
        &ngx_live_channels.rbtree, id, hash);
    if (channel != NULL) {
        *result = channel;
        return NGX_EXISTS;
    }

    /* allocate the channel */
    pool = ngx_create_pool(4096, ngx_cycle->log);
    if (pool == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_live_channel_create: create pool failed");
        return NGX_ERROR;
    }

    channel = ngx_palloc(pool, sizeof(*channel) + id->len);
    if (channel == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_live_channel_create: alloc channel failed");
        goto error;
    }

    ngx_memzero(channel, sizeof(*channel));

    channel->ctx = ngx_pcalloc(pool, sizeof(void *) * ngx_live_max_module);
    if (channel->ctx == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_live_channel_create: alloc ctx failed");
        goto error;
    }

    /* create block pool */
    cpcf = ngx_live_get_module_preset_conf(conf_ctx, ngx_live_core_module);

    channel->block_pool = ngx_block_pool_create(pool, cpcf->mem_blocks.elts,
        cpcf->mem_blocks.nelts, &channel->mem_left);
    if (channel->block_pool == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_live_channel_create: create block pool failed");
        goto error;
    }

    channel->bp_idx = cpcf->bp_idx;

    /* initialize */
    channel->pool = pool;

    channel->sn.str.data = (void *) (channel + 1);
    channel->sn.str.len = id->len;
    ngx_memcpy(channel->sn.str.data, id->data, channel->sn.str.len);
    channel->id_escape = ngx_json_str_get_escape(id);

    channel->sn.node.key = hash;

    ngx_live_random_bytes(temp_pool->log, &channel->uid, sizeof(channel->uid));

    channel->log = *pool->log;
    pool->log = &channel->log;

    channel->log.handler = ngx_live_channel_log_error;
    channel->log.data = channel;

    channel->main_conf = conf_ctx->main_conf;
    channel->preset_conf = conf_ctx->preset_conf;

    channel->start_sec = ngx_time();
    channel->last_modified = ngx_time();

    /* call handlers */
    ngx_live_core_channel_init(channel);

    rc = ngx_live_core_channel_event(channel, NGX_LIVE_EVENT_CHANNEL_INIT,
        NULL);
    if (rc != NGX_OK) {
        (void) ngx_live_core_channel_event(channel,
            NGX_LIVE_EVENT_CHANNEL_FREE, NULL);
        goto error;
    }

    /* initialize trees/queues */
    ngx_queue_init(&channel->tracks.queue);
    ngx_rbtree_init(&channel->tracks.rbtree, &channel->tracks.sentinel,
        ngx_str_rbtree_insert_value);
    ngx_rbtree_init(&channel->tracks.irbtree, &channel->tracks.isentinel,
        ngx_rbtree_insert_value);

    ngx_queue_init(&channel->variants.queue);
    ngx_rbtree_init(&channel->variants.rbtree, &channel->variants.sentinel,
        ngx_str_rbtree_insert_value);

    ngx_rbtree_insert(&ngx_live_channels.rbtree, &channel->sn.node);
    ngx_queue_insert_tail(&ngx_live_channels.queue, &channel->queue);

    ngx_log_error(NGX_LOG_INFO, &channel->log, 0,
        "ngx_live_channel_create: created %p, uid: %016uxL",
        channel, channel->uid);

    *result = channel;

    return NGX_OK;

error:

    ngx_destroy_pool(pool);
    return NGX_ERROR;
}


static void
ngx_live_channel_free_internal(ngx_live_channel_t *channel)
{
    ngx_queue_t       *q;
    ngx_live_track_t  *cur_track;

    ngx_log_error(NGX_LOG_INFO, &channel->log, 0,
        "ngx_live_channel_free_internal: freeing %p, reason: %d",
        channel, channel->free_reason);

    for (q = ngx_queue_head(&channel->tracks.queue);
        q != ngx_queue_sentinel(&channel->tracks.queue);
        q = ngx_queue_next(q))
    {
        cur_track = ngx_queue_data(q, ngx_live_track_t, queue);

        ngx_live_track_channel_free(cur_track,
            NGX_LIVE_EVENT_TRACK_CHANNEL_FREE);
    }

    (void) ngx_live_core_channel_event(channel, NGX_LIVE_EVENT_CHANNEL_FREE,
        NULL);

    ngx_rbtree_delete(&ngx_live_channels.rbtree, &channel->sn.node);
    ngx_queue_remove(&channel->queue);

    ngx_destroy_pool(channel->pool);
}


void
ngx_live_channel_free(ngx_live_channel_t *channel,
    ngx_live_free_reason_e reason)
{
    if (channel->free_reason) {
        return;
    }

    channel->free_reason = reason;

    ngx_live_channel_free_internal(channel);
}


static void
ngx_live_channel_close_handler(ngx_event_t *ev)
{
    ngx_live_channel_t  *channel;

    channel = ev->data;

    ngx_live_channel_free_internal(channel);
}


void
ngx_live_channel_finalize(ngx_live_channel_t *channel,
    ngx_live_free_reason_e reason)
{
    ngx_event_t  *e;

    if (channel->free_reason) {
        return;
    }

    channel->free_reason = reason;

    e = &channel->close;
    e->data = channel;
    e->handler = ngx_live_channel_close_handler;
    e->log = &channel->log;

    ngx_post_event(e, &ngx_posted_events);
}


static ngx_flag_t
ngx_live_channel_has_pending_segments(ngx_live_channel_t *channel)
{
    ngx_queue_t       *q;
    ngx_live_track_t  *cur_track;

    for (q = ngx_queue_head(&channel->tracks.queue);
        q != ngx_queue_sentinel(&channel->tracks.queue);
        q = ngx_queue_next(q))
    {
        cur_track = ngx_queue_data(q, ngx_live_track_t, queue);

        if (cur_track->pending_index > 0 || cur_track->has_pending_segment) {
            return 1;
        }
    }

    return 0;
}


ngx_int_t
ngx_live_channel_update(ngx_live_channel_t *channel,
    ngx_live_channel_conf_t *conf)
{
    ngx_live_channel_conf_t  old_conf;

    if (channel->next_segment_index != 0
        || ngx_live_channel_has_pending_segments(channel))
    {
        /* initial segment index cant be changed once there are segments */
        conf->initial_segment_index = channel->conf.initial_segment_index;
    }

    if (ngx_memcmp(conf, &channel->conf, sizeof(*conf)) == 0) {
        return NGX_OK;
    }

    old_conf = channel->conf;

    channel->conf = *conf;
    channel->segment_duration = ngx_live_rescale_time(
        channel->conf.segment_duration, 1000, channel->timescale);

    if (ngx_live_core_channel_event(channel,
        NGX_LIVE_EVENT_CHANNEL_CONF_CHANGED, NULL) != NGX_OK)
    {
        goto revert;
    }

    if (old_conf.initial_segment_index != conf->initial_segment_index) {
        channel->next_segment_index = conf->initial_segment_index;
    }

    return NGX_OK;

revert:

    channel->conf = old_conf;
    channel->segment_duration = ngx_live_rescale_time(
        channel->conf.segment_duration, 1000, channel->timescale);

    (void) ngx_live_core_channel_event(channel,
        NGX_LIVE_EVENT_CHANNEL_CONF_CHANGED, NULL);
    return NGX_ERROR;
}


void
ngx_live_channel_setup_changed(ngx_live_channel_t *channel)
{
    channel->last_modified = ngx_time();

    (void) ngx_live_core_channel_event(channel,
        NGX_LIVE_EVENT_CHANNEL_SETUP_CHANGED, NULL);
}


void
ngx_live_channel_ack_frames(ngx_live_channel_t *channel)
{
    ngx_queue_t       *q;
    ngx_live_track_t  *cur_track;

    ngx_log_error(NGX_LOG_INFO, &channel->log, 0,
        "ngx_live_channel_ack_frames: sending acks");

    for (q = ngx_queue_head(&channel->tracks.queue);
        q != ngx_queue_sentinel(&channel->tracks.queue);
        q = ngx_queue_next(q))
    {
        cur_track = ngx_queue_data(q, ngx_live_track_t, queue);

        if (cur_track->input != NULL) {
            ngx_kmp_in_ack_frames(cur_track->input, cur_track->next_frame_id);
        }
    }
}


ngx_buf_chain_t *
ngx_live_channel_copy_chains(ngx_live_channel_t *channel,
    ngx_buf_chain_t *src, size_t size, ngx_buf_chain_t **out_tail)
{
    ngx_buf_chain_t   *dst;
    ngx_buf_chain_t   *head, *tail;
    ngx_buf_chain_t  **last;

    dst = NULL;
    last = &head;

    while (size > 0) {

#if (NGX_LIVE_VALIDATIONS)
        if (size < src->size) {
            ngx_log_error(NGX_LOG_ALERT, &channel->log, 0,
                "ngx_live_channel_copy_chains: "
                "size left %uz smaller than buf chain size %uz",
                size, src->size);
            ngx_debug_point();
        }
#endif

        dst = ngx_live_channel_buf_chain_alloc(channel);
        if (dst == NULL) {
            goto failed;
        }

        *last = dst;
        last = &dst->next;

        dst->data = src->data;
        dst->size = src->size;

        size -= src->size;
        src = src->next;
    }

    *last = NULL;

    if (out_tail != NULL) {
        *out_tail = dst;
    }

    return head;

failed:

    if (last != &head) {
        *last = NULL;

        tail = (ngx_buf_chain_t *) last; /* next is first in ngx_buf_chain_t */
        ngx_live_channel_buf_chain_free_list(channel, head, tail);
    }

    return NULL;
}


ngx_int_t
ngx_live_channel_block_str_set(ngx_live_channel_t *channel,
    ngx_block_str_t *dest, ngx_str_t *src)
{
    ngx_int_t  rc;

    rc = ngx_block_str_set(dest, channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_STR], src);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_live_channel_setup_changed(channel);

    return NGX_OK;
}


static void
ngx_live_channel_block_str_free(ngx_live_channel_t *channel,
    ngx_block_str_t *str)
{
    ngx_block_str_free(str, channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_STR]);
}


ngx_int_t
ngx_live_channel_block_str_read(ngx_live_channel_t *channel,
    ngx_block_str_t *dest, ngx_mem_rstream_t *rs)
{
    return ngx_block_str_read(rs, dest, channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_STR]);
}


/* variant */

static ngx_int_t
ngx_live_variant_validate_conf(ngx_live_variant_conf_t *conf, ngx_log_t *log)
{
    if (conf->label.s.len > NGX_LIVE_VARIANT_MAX_LABEL_LEN) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_variant_validate_conf: label \"%V\" too long",
            &conf->label);
        return NGX_ERROR;
    }

    if (conf->lang.s.len > NGX_LIVE_VARIANT_MAX_LANG_LEN) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_variant_validate_conf: lang \"%V\" too long",
            &conf->lang);
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_live_variant_t *
ngx_live_variant_get(ngx_live_channel_t *channel, ngx_str_t *id)
{
    uint32_t  hash;

    hash = ngx_crc32_short(id->data, id->len);
    return (ngx_live_variant_t *) ngx_str_rbtree_lookup(
        &channel->variants.rbtree, id, hash);
}


static ngx_int_t
ngx_live_variant_validate_id(ngx_str_t *id, ngx_log_t *log)
{
    size_t  i;
    u_char  ch;

    if (id->len > NGX_LIVE_VARIANT_MAX_ID_LEN) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_variant_validate_id: variant id \"%V\" too long", id);
        return NGX_ERROR;
    }

    for (i = 0; i < id->len; i++) {

        ch = id->data[i];
        switch (ch) {

        case '/':   /* delimits path params in live api */
        case '-':   /* delimits variant ids in ksmp requests
                        + uri file name params on the packager */
            ngx_log_error(NGX_LOG_ERR, log, 0,
                "ngx_live_variant_validate_id: "
                "invalid variant id \"%V\", must not contain \"%c\"", id, ch);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_live_variant_create(ngx_live_channel_t *channel, ngx_str_t *id,
    ngx_live_variant_conf_t *conf, ngx_log_t *log, ngx_live_variant_t **result)
{
    uint32_t             hash;
    ngx_live_variant_t  *variant;

    if (ngx_live_variant_validate_id(id, log) != NGX_OK) {
        return NGX_INVALID_ARG;
    }

    if (ngx_live_variant_validate_conf(conf, log) != NGX_OK) {
        return NGX_INVALID_ARG;
    }

    hash = ngx_crc32_short(id->data, id->len);
    variant = (ngx_live_variant_t *) ngx_str_rbtree_lookup(
        &channel->variants.rbtree, id, hash);
    if (variant != NULL) {
        *result = variant;
        return NGX_EXISTS;
    }

    variant = ngx_block_pool_calloc(channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_VARIANT]);
    if (variant == NULL) {

        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_variant_create: alloc failed");
        return NGX_ERROR;
    }

    variant->channel = channel;

    variant->sn.str.data = variant->id_buf;
    variant->sn.str.len = id->len;
    ngx_memcpy(variant->sn.str.data, id->data, variant->sn.str.len);
    variant->id_escape = ngx_json_str_get_escape(id);

    variant->sn.node.key = hash;

    variant->conf.label.s.data = variant->label_buf;
    if (conf->label.s.len > 0) {
        variant->conf.label.s.len = conf->label.s.len;
        ngx_memcpy(variant->label_buf, conf->label.s.data, conf->label.s.len);
        ngx_json_str_set_escape(&variant->conf.label);
    }

    variant->conf.lang.s.data = variant->lang_buf;
    if (conf->lang.s.len > 0) {
        variant->conf.lang.s.len = conf->lang.s.len;
        ngx_memcpy(variant->lang_buf, conf->lang.s.data, conf->lang.s.len);
        ngx_json_str_set_escape(&variant->conf.lang);
    }

    variant->conf.role = conf->role;
    variant->conf.is_default = conf->is_default;

    variant->initial_segment_index = NGX_LIVE_INVALID_SEGMENT_INDEX;

    ngx_rbtree_insert(&channel->variants.rbtree, &variant->sn.node);
    ngx_queue_insert_tail(&channel->variants.queue, &variant->queue);

    channel->variants.count++;

    ngx_log_error(NGX_LOG_INFO, &channel->log, 0,
        "ngx_live_variant_create: created %p, variant: %V",
        variant, &variant->sn.str);

    ngx_live_channel_setup_changed(channel);

    *result = variant;

    return NGX_OK;
}


void
ngx_live_variant_free(ngx_live_variant_t *variant)
{
    ngx_live_channel_t  *channel = variant->channel;

    ngx_log_error(NGX_LOG_INFO, &channel->log, 0,
        "ngx_live_variant_free: freeing %p", variant);

    channel->variants.count--;

    ngx_live_channel_block_str_free(channel, &variant->opaque);

    ngx_rbtree_delete(&channel->variants.rbtree, &variant->sn.node);
    ngx_queue_remove(&variant->queue);

    ngx_block_pool_free(channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_VARIANT], variant);

    ngx_live_channel_setup_changed(channel);
}


ngx_int_t
ngx_live_variant_update(ngx_live_variant_t *variant,
    ngx_live_variant_conf_t *conf, ngx_log_t *log)
{
    if (ngx_live_variant_validate_conf(conf, log) != NGX_OK) {
        return NGX_ERROR;
    }

    variant->conf.label.s.len = conf->label.s.len;
    if (conf->label.s.data != variant->label_buf) {
        ngx_memcpy(variant->label_buf, conf->label.s.data, conf->label.s.len);
    }

    ngx_json_str_set_escape(&variant->conf.label);

    variant->conf.lang.s.len = conf->lang.s.len;
    if (conf->lang.s.data != variant->lang_buf) {
        ngx_memcpy(variant->lang_buf, conf->lang.s.data, conf->lang.s.len);
    }

    ngx_json_str_set_escape(&variant->conf.lang);

    variant->conf.role = conf->role;
    variant->conf.is_default = conf->is_default;

    ngx_live_channel_setup_changed(variant->channel);

    return NGX_OK;
}


ngx_int_t
ngx_live_variant_set_track(ngx_live_variant_t *variant,
    ngx_live_track_t *track, ngx_log_t *log)
{
    if (track->type == ngx_live_track_type_filler) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_variant_set_track: "
            "track \"%V\" in channel \"%V\" is a filler track",
            &track->sn.str, &track->channel->sn.str);
        return NGX_ERROR;
    }

    if (variant->tracks[track->media_type] == NULL) {
        variant->track_count++;
    }

    variant->tracks[track->media_type] = track;

    ngx_live_channel_setup_changed(variant->channel);

    return NGX_OK;
}


ngx_int_t
ngx_live_variant_set_tracks(ngx_live_variant_t *variant,
    ngx_live_track_t **tracks, ngx_log_t *log)
{
    uint32_t           i;
    uint32_t           track_count;
    ngx_live_track_t  *cur_track;

    track_count = 0;
    for (i = 0; i < KMP_MEDIA_COUNT; i++) {
        cur_track = tracks[i];
        if (cur_track == NULL) {
            continue;
        }

        if (cur_track->type == ngx_live_track_type_filler) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                "ngx_live_variant_set_tracks: "
                "track \"%V\" in channel \"%V\" is a filler track",
                &cur_track->sn.str, &cur_track->channel->sn.str);
            return NGX_ERROR;
        }

        track_count++;
    }

    ngx_memcpy(variant->tracks, tracks, sizeof(variant->tracks));
    variant->track_count = track_count;

    return NGX_OK;
}


static void
ngx_live_variant_update_active(ngx_live_variant_t *variant)
{
    uint32_t             media_type_flag;
    ngx_uint_t           media_type;
    ngx_live_track_t    *cur_track;
    ngx_live_channel_t  *channel;

    channel = variant->channel;

    for (media_type = 0; media_type < KMP_MEDIA_SUBTITLE; media_type++) {

        cur_track = variant->tracks[media_type];
        if (cur_track == NULL) {
            continue;
        }

        if (cur_track->has_last_segment || cur_track->pending_index > 0
            || cur_track->has_pending_segment)
        {
            goto active;
        }

        media_type_flag = 1 << media_type;
        if (channel->last_segment_media_types & media_type_flag) {
            break;
        }

        if ((channel->filler_media_types & media_type_flag) &&
            ngx_live_media_info_queue_get_last(cur_track))
        {
            goto active;
        }
    }

    cur_track = variant->tracks[KMP_MEDIA_SUBTITLE];
    if (cur_track != NULL) {
        goto active;
    }

    variant->active = 0;

    return;

active:

    variant->active = 1;

    if (variant->initial_segment_index == NGX_LIVE_INVALID_SEGMENT_INDEX) {
        variant->initial_segment_index = cur_track->initial_segment_index;
    }
}


void
ngx_live_variants_update_active(ngx_live_channel_t *channel)
{
    ngx_queue_t         *q;
    ngx_live_variant_t  *cur_variant;

    for (q = ngx_queue_head(&channel->variants.queue);
        q != ngx_queue_sentinel(&channel->variants.queue);
        q = ngx_queue_next(q))
    {
        cur_variant = ngx_queue_data(q, ngx_live_variant_t, queue);

        ngx_live_variant_update_active(cur_variant);
    }
}


ngx_flag_t
ngx_live_variant_is_active_last(ngx_live_variant_t *variant,
    ngx_live_timeline_t *timeline)
{
    uint32_t             track_id;
    uint32_t             segment_index;
    ngx_uint_t           media_type;
    ngx_queue_t         *q;
    ngx_live_track_t    *ref_track;
    ngx_live_track_t    *cur_track;
    ngx_live_period_t   *period;
    ngx_live_channel_t  *channel;

    q = ngx_queue_last(&timeline->periods);
    if (q == ngx_queue_sentinel(&timeline->periods)) {
        return 0;
    }

    period = ngx_queue_data(q, ngx_live_period_t, queue);
    segment_index = period->node.key + period->segment_count - 1;

    channel = variant->channel;

    if (segment_index + 1 >= channel->next_segment_index) {
        /* more optimized impl. when the timeline has the last segment */
        return variant->active;
    }

    for (media_type = 0; media_type < KMP_MEDIA_SUBTITLE; media_type++) {

        cur_track = variant->tracks[media_type];
        if (cur_track == NULL) {
            continue;
        }

        if (!ngx_live_segment_info_segment_exists(cur_track, segment_index,
            segment_index + 1))
        {
            continue;
        }

        if (ngx_live_media_info_queue_get_node(cur_track, segment_index,
            &track_id) == NULL)
        {
            return 0;
        }

        if (track_id == cur_track->in.key) {
            /* own segment */
            return 1;
        }

        ref_track = ngx_live_track_get_by_int(channel, track_id);
        if (ref_track != NULL &&
            ref_track->type == ngx_live_track_type_filler)
        {
            /* filler segment */
            return 1;
        }

        /* other track segment */
        return 0;
    }

    cur_track = variant->tracks[KMP_MEDIA_SUBTITLE];
    if (cur_track != NULL) {
        return 1;
    }

    return 0;
}


uint32_t
ngx_live_variant_is_active_any(ngx_live_variant_t *variant,
    ngx_live_timeline_t *timeline, uint32_t req_media_types)
{
    uint32_t             segment_index;
    uint32_t             res_media_types;
    uint32_t             media_type_flag;
    ngx_uint_t           media_type;
    ngx_queue_t         *q;
    ngx_live_track_t    *cur_track;
    ngx_live_period_t   *period;
    ngx_live_channel_t  *channel;

    q = ngx_queue_last(&timeline->periods);
    if (q == ngx_queue_sentinel(&timeline->periods)) {
        return 0;
    }

    period = ngx_queue_data(q, ngx_live_period_t, queue);
    segment_index = period->node.key + period->segment_count - 1;

    channel = variant->channel;

    res_media_types = 0;

    for (media_type = 0; media_type < KMP_MEDIA_COUNT; media_type++) {

        media_type_flag = 1 << media_type;
        if (!(req_media_types & media_type_flag)) {
            continue;
        }

        cur_track = variant->tracks[media_type];
        if (cur_track == NULL) {
            continue;
        }

        if (segment_index + 1 == channel->next_segment_index &&
            cur_track->has_last_segment)
        {
            /* when both timeline and track have the last segment,
                no need to search */
            res_media_types |= media_type_flag;
            continue;
        }

        if (!ngx_live_segment_info_timeline_exists(cur_track, timeline)) {
            continue;
        }

        if (ngx_live_media_info_track_exists(timeline, cur_track)) {
            res_media_types |= media_type_flag;
            continue;
        }

        if (!res_media_types) {
            return 0;
        }
    }

    return res_media_types;
}


static size_t
ngx_live_variant_json_track_ids_get_size(ngx_live_variant_t *obj)
{
    size_t             result = 0;
    uint32_t           media_type;
    ngx_live_track_t  *cur_track;

    for (media_type = 0; media_type < KMP_MEDIA_COUNT; media_type++) {

        cur_track = obj->tracks[media_type];
        if (cur_track == NULL) {
            continue;
        }

        result += sizeof("\"\":\"\",") - 1 +
            ngx_live_track_media_type_names[media_type].len +
            cur_track->sn.str.len + cur_track->id_escape;
    }

    return result;
}


static u_char *
ngx_live_variant_json_track_ids_write(u_char *p, ngx_live_variant_t *obj)
{
    uint32_t           media_type;
    ngx_flag_t         comma;
    ngx_live_track_t  *cur_track;

    comma = 0;
    for (media_type = 0; media_type < KMP_MEDIA_COUNT; media_type++) {

        cur_track = obj->tracks[media_type];
        if (cur_track == NULL) {
            continue;
        }

        if (comma) {
            *p++ = ',';

        } else {
            comma = 1;
        }

        *p++ = '"';
        p = ngx_copy_str(p, ngx_live_track_media_type_names[media_type]);
        *p++ = '"';

        *p++ = ':';

        *p++ = '"';
        p = ngx_json_str_write_escape(p, &cur_track->sn.str,
            cur_track->id_escape);
        *p++ = '"';
    }

    return p;
}


/* track */

ngx_live_track_t *
ngx_live_track_get(ngx_live_channel_t *channel, ngx_str_t *id)
{
    uint32_t  hash;

    hash = ngx_crc32_short(id->data, id->len);
    return (ngx_live_track_t *) ngx_str_rbtree_lookup(&channel->tracks.rbtree,
        id, hash);
}


ngx_live_track_t *
ngx_live_track_get_by_int(ngx_live_channel_t *channel, uint32_t id)
{
    ngx_rbtree_t       *rbtree;
    ngx_rbtree_node_t  *node, *sentinel;

    rbtree = &channel->tracks.irbtree;

    node = rbtree->root;
    sentinel = rbtree->sentinel;

    while (node != sentinel) {

        if (id < node->key) {
            node = node->left;
            continue;
        }

        if (id > node->key) {
            node = node->right;
            continue;
        }

        return ngx_rbtree_data(node, ngx_live_track_t, in);
    }

    return NULL;
}


static u_char *
ngx_live_track_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    u_char              *p;
    ngx_live_track_t    *track;
    ngx_live_channel_t  *channel;

    p = buf;

    track = log->data;

    if (track != NULL) {

        channel = track->channel;

        p = ngx_snprintf(buf, len, ", nsi: %uD, track: %V, channel: %V",
            channel->next_segment_index, &track->sn.str, &channel->sn.str);
    }

    return p;
}


ngx_int_t
ngx_live_track_create(ngx_live_channel_t *channel, ngx_str_t *id,
    uint32_t int_id, uint32_t media_type, ngx_log_t *log,
    ngx_live_track_t **result)
{
    uint32_t                      hash;
    ngx_int_t                     rc;
    ngx_uint_t                    i;
    ngx_queue_t                  *q;
    ngx_live_track_t             *track;
    ngx_live_track_t             *cur_track;
    ngx_live_core_ctx_offset_t   *offsets;
    ngx_live_core_preset_conf_t  *cpcf;

    if (id->len > sizeof(track->id_buf)) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_track_create: track id \"%V\" too long", id);
        return NGX_INVALID_ARG;
    }

    if (media_type >= KMP_MEDIA_COUNT) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_track_create: invalid media type %uD", media_type);
        return NGX_INVALID_ARG;
    }

    hash = ngx_crc32_short(id->data, id->len);
    track = (ngx_live_track_t *) ngx_str_rbtree_lookup(&channel->tracks.rbtree,
        id, hash);
    if (track != NULL) {

        if (track->media_type != media_type) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                "ngx_live_track_create: "
                "attempt to change track type from %uD to %uD",
                track->media_type, media_type);
            return NGX_INVALID_ARG;
        }

        if (int_id != NGX_LIVE_INVALID_TRACK_ID && track->in.key != int_id) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                "ngx_live_track_create: "
                "attempt to change int id from %ui to %uD",
                track->in.key, int_id);
            return NGX_INVALID_ARG;
        }

        *result = track;
        return NGX_EXISTS;
    }

    if (int_id != NGX_LIVE_INVALID_TRACK_ID) {

        if (ngx_live_track_get_by_int(channel, int_id) != NULL) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                "ngx_live_track_create: int id %uD already used", int_id);
            return NGX_INVALID_ARG;
        }

        if (channel->tracks.last_id < int_id) {
            channel->tracks.last_id = int_id;
        }

    } else {
        int_id = ++channel->tracks.last_id;
    }


    track = ngx_block_pool_calloc(channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_TRACK]);
    if (track == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
            "ngx_live_track_create: alloc failed");
        return NGX_ERROR;
    }

    track->channel = channel;

    track->sn.str.data = track->id_buf;
    track->sn.str.len = id->len;
    ngx_memcpy(track->sn.str.data, id->data, track->sn.str.len);
    track->id_escape = ngx_json_str_get_escape(id);

    track->sn.node.key = hash;
    track->in.key = int_id;

    track->log = channel->log;
    track->log.handler = ngx_live_track_log_error;
    track->log.data = track;
    track->start_sec = ngx_time();
    track->media_type = media_type;

    track->last_frame_pts = NGX_LIVE_INVALID_TIMESTAMP;
    track->last_frame_dts = NGX_LIVE_INVALID_TIMESTAMP;

    cpcf = ngx_live_get_module_preset_conf(channel, ngx_live_core_module);

    track->ctx = (void *) (track + 1);

    offsets = cpcf->track_ctx_offset.elts;
    for (i = 0; i < cpcf->track_ctx_offset.nelts; i++) {
        track->ctx[offsets[i].index] = (u_char *) track + offsets[i].offset;
    }

    track->initial_segment_index = NGX_LIVE_INVALID_SEGMENT_INDEX;

    rc = ngx_live_core_track_event(track, NGX_LIVE_EVENT_TRACK_INIT, NULL);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, log, 0,
            "ngx_live_track_create: track init failed %i", rc);
        (void) ngx_live_core_track_event(track, NGX_LIVE_EVENT_TRACK_FREE,
            NULL);
        ngx_block_pool_free(channel->block_pool,
            channel->bp_idx[NGX_LIVE_CORE_BP_TRACK], track);
        return rc;
    }

    ngx_rbtree_insert(&channel->tracks.rbtree, &track->sn.node);
    ngx_rbtree_insert(&channel->tracks.irbtree, &track->in);

    /* insert to queue in media type order */
    for (q = ngx_queue_head(&channel->tracks.queue);
        q != ngx_queue_sentinel(&channel->tracks.queue);
        q = ngx_queue_next(q))
    {
        cur_track = ngx_queue_data(q, ngx_live_track_t, queue);
        if (media_type < cur_track->media_type) {
            break;
        }
    }

    ngx_queue_insert_before(q, &track->queue);

    channel->tracks.count++;

    ngx_log_error(NGX_LOG_INFO, &track->log, 0,
        "ngx_live_track_create: created %p, id %ui", track, track->in.key);

    ngx_live_channel_setup_changed(channel);

    *result = track;

    return NGX_OK;
}


static void
ngx_live_track_channel_free(ngx_live_track_t *track, ngx_uint_t event)
{
    (void) ngx_live_core_track_event(track, event, NULL);

    if (track->input != NULL) {
        track->input->disconnect(track->input, NGX_OK);
    }
}


void
ngx_live_track_free(ngx_live_track_t *track)
{
    uint32_t             media_type;
    ngx_queue_t         *q;
    ngx_live_variant_t  *cur_variant;
    ngx_live_channel_t  *channel = track->channel;

    ngx_log_error(NGX_LOG_INFO, &track->log, 0,
        "ngx_live_track_free: freeing %p", track);

    /* remove from all variants */
    media_type = track->media_type;
    for (q = ngx_queue_head(&channel->variants.queue);
        q != ngx_queue_sentinel(&channel->variants.queue);
        q = ngx_queue_next(q))
    {
        cur_variant = ngx_queue_data(q, ngx_live_variant_t, queue);

        if (cur_variant->tracks[media_type] != track) {
            continue;
        }

        cur_variant->tracks[media_type] = NULL;
        cur_variant->track_count--;
    }

    channel->tracks.count--;

    ngx_live_track_channel_free(track, NGX_LIVE_EVENT_TRACK_FREE);

    ngx_live_channel_block_str_free(channel, &track->opaque);

    ngx_rbtree_delete(&channel->tracks.rbtree, &track->sn.node);
    ngx_rbtree_delete(&channel->tracks.irbtree, &track->in);
    ngx_queue_remove(&track->queue);

    ngx_block_pool_free(channel->block_pool,
        channel->bp_idx[NGX_LIVE_CORE_BP_TRACK], track);

    ngx_live_channel_setup_changed(channel);
}
