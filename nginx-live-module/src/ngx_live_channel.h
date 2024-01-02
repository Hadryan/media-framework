#ifndef _NGX_LIVE_CHANNEL_H_INCLUDED_
#define _NGX_LIVE_CHANNEL_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_buf_queue.h>
#include <ngx_buf_chain.h>
#include <ngx_kmp_in.h>
#include "ngx_live_config.h"
#include "ngx_block_pool.h"
#include "ngx_block_str.h"


#define NGX_INVALID_ARG                    NGX_DECLINED
#define NGX_EXISTS                         NGX_BUSY

#define NGX_LIVE_VARIANT_MAX_ID_LEN        (32)
#define NGX_LIVE_VARIANT_MAX_LABEL_LEN     (64)
#define NGX_LIVE_VARIANT_MAX_LANG_LEN      (6)

#define NGX_LIVE_TRACK_MAX_ID_LEN          (KMP_MAX_TRACK_ID_LEN)

#define NGX_LIVE_INVALID_TRACK_ID          (0)
#define NGX_LIVE_INVALID_SEGMENT_INDEX     (NGX_KSMP_INVALID_SEGMENT_INDEX)
#define NGX_LIVE_INVALID_PART_INDEX        (NGX_KSMP_INVALID_PART_INDEX)
#define NGX_LIVE_INVALID_TIMESTAMP         (NGX_KSMP_INVALID_TIMESTAMP)

#define NGX_LIVE_PENDING_SEGMENT_DURATION  (NGX_KSMP_PENDING_SEGMENT_DURATION)

#define NGX_LIVE_SEGMENT_NO_BITRATE        (NGX_KSMP_SEGMENT_NO_BITRATE)


#define ngx_live_channel_auto_alloc(channel, size)                           \
    ngx_block_pool_auto_alloc((channel)->block_pool, (size))

#define ngx_live_channel_auto_free(channel, ptr)                             \
    ngx_block_pool_auto_free((channel)->block_pool, (ptr))


#define ngx_live_channel_buf_chain_alloc(ch)                                 \
    ngx_block_pool_alloc((ch)->block_pool,                                   \
        (ch)->bp_idx[NGX_LIVE_CORE_BP_BUF_CHAIN])

#define ngx_live_channel_buf_chain_free_list(ch, head, tail)                 \
    ngx_block_pool_free_list((ch)->block_pool,                               \
        (ch)->bp_idx[NGX_LIVE_CORE_BP_BUF_CHAIN], (head), (tail))


typedef void (*ngx_live_track_ack_frames_pt)(void *data,
    uint64_t next_frame_id);

typedef void (*ngx_live_track_disconnect_pt)(void *data, ngx_uint_t rc);


typedef enum {
    ngx_live_free_none,
    ngx_live_free_api,

    ngx_live_free_update_failed,
    ngx_live_free_read_failed,
    ngx_live_free_read_cancelled,

    ngx_live_free_alloc_chain_failed,
    ngx_live_free_alloc_buf_failed,
    ngx_live_free_add_media_info_failed,
    ngx_live_free_add_frame_failed,

    ngx_live_free_create_segment_failed,

    ngx_live_free_process_frame_failed,
    ngx_live_free_flush_segment_failed,
    ngx_live_free_close_segment_failed,
} ngx_live_free_reason_e;


typedef struct {
    ngx_rbtree_t                   rbtree;
    ngx_rbtree_node_t              sentinel;
    uint32_t                       count;
    ngx_queue_t                    queue;
} ngx_live_channel_variants_t;


typedef struct {
    ngx_rbtree_t                   rbtree;        /* by string id */
    ngx_rbtree_node_t              sentinel;
    ngx_rbtree_t                   irbtree;       /* by int id */
    ngx_rbtree_node_t              isentinel;
    ngx_queue_t                    queue;
    uint32_t                       count;
    uint32_t                       last_id;
} ngx_live_channel_tracks_t;


typedef struct {
    ngx_msec_t                     segment_duration;
    ngx_msec_t                     input_delay;
    uint32_t                       initial_segment_index;
} ngx_live_channel_conf_t;


struct ngx_live_channel_s {
    ngx_str_node_t                 sn;        /* must be first */
    uint64_t                       uid;
    ngx_queue_t                    queue;
    uintptr_t                      id_escape;
    ngx_block_str_t                opaque;

    ngx_block_pool_t              *block_pool;
    ngx_uint_t                    *bp_idx;
    ngx_pool_t                    *pool;
    ngx_log_t                      log;

    size_t                         mem_limit;
    size_t                         mem_left;
    size_t                         mem_high_watermark;
    size_t                         mem_low_watermark;
    ngx_uint_t                     mem_watermark_events;

    time_t                         last_modified;
    time_t                         last_accessed;

    time_t                         start_sec;
    time_t                         read_time;
    ngx_event_t                    close;
    ngx_live_free_reason_e         free_reason;

    void                         **ctx;
    void                         **main_conf;
    void                         **preset_conf;

    ngx_live_channel_conf_t        conf;
    ngx_msec_t                     input_delay_margin;

    ngx_uint_t                     timescale;
    uint32_t                       segment_duration;    /* sec / timescale */
    uint32_t                       part_duration;       /* sec / timescale */

    ngx_live_channel_variants_t    variants;

    ngx_live_channel_tracks_t      tracks;

    uint32_t                       filler_start_index;
    uint32_t                       filler_media_types;

    uint32_t                       min_segment_index;
    uint32_t                       next_segment_index;
    uint32_t                       next_part_sequence;
    uint32_t                       last_segment_media_types;
    time_t                         last_segment_created;

    uint32_t                       snapshots;
    unsigned                       blocked:8;
    unsigned                       active:1;
};


typedef enum {
    ngx_live_track_type_default,
    ngx_live_track_type_filler,
} ngx_live_track_type_e;


struct ngx_live_track_s {
    ngx_str_node_t                 sn;        /* must be first */
    ngx_rbtree_node_t              in;

    ngx_queue_t                    queue;
    ngx_live_channel_t            *channel;
    u_char                         id_buf[NGX_LIVE_TRACK_MAX_ID_LEN];
    uintptr_t                      id_escape;
    ngx_block_str_t                opaque;

    uint32_t                       media_type;
    time_t                         start_sec;
    ngx_live_track_type_e          type;

    void                         **ctx;
    ngx_log_t                      log;

    ngx_kmp_in_ctx_t              *input;

    /* Note: the fields below can be read only while handling the events:
        index ready / snapshot, track reconnect, read from persist */
    int64_t                        last_frame_pts;
    int64_t                        last_frame_dts;
    uint64_t                       next_frame_id;

    uint32_t                       initial_segment_index;
    uint32_t                       pending_index;
    uint32_t                       next_part_index;

    ngx_live_media_info_node_t    *media_info_node;     /* temp during serve */

    /* Note: when a track gets a segment from another track (gap filling),
        has_last_segment = 0 while last_segment_bitrate != 0 */
    uint32_t                       last_segment_bitrate;
    unsigned                       has_last_segment:1;
    unsigned                       has_pending_segment:1;
};


typedef struct {
    ngx_json_str_t                 label;
    ngx_json_str_t                 lang;
    ngx_ksmp_variant_role_e        role;
    unsigned                       is_default:1;
} ngx_live_variant_conf_t;


typedef struct {
    ngx_str_node_t                 sn;        /* must be first */
    ngx_queue_t                    queue;
    ngx_live_channel_t            *channel;
    u_char                         id_buf[NGX_LIVE_VARIANT_MAX_ID_LEN];
    uintptr_t                      id_escape;
    ngx_block_str_t                opaque;

    ngx_live_track_t              *tracks[KMP_MEDIA_COUNT];
    uint32_t                       track_count;

    uint32_t                       initial_segment_index;

    ngx_live_variant_conf_t        conf;
    u_char                         label_buf[NGX_LIVE_VARIANT_MAX_LABEL_LEN];
    u_char                         lang_buf[NGX_LIVE_VARIANT_MAX_LANG_LEN];

    uint32_t                       output_media_types;  /* temp during serve */

    unsigned                       active:1;
} ngx_live_variant_t;


ngx_int_t ngx_live_channel_init_process(ngx_cycle_t *cycle);


/* channel */
ngx_int_t ngx_live_channel_create(ngx_str_t *id, ngx_live_conf_ctx_t *conf_ctx,
    ngx_pool_t *temp_pool, ngx_live_channel_t **result);

void ngx_live_channel_free(ngx_live_channel_t *channel,
    ngx_live_free_reason_e reason);

void ngx_live_channel_finalize(ngx_live_channel_t *channel,
    ngx_live_free_reason_e reason);

ngx_live_channel_t *ngx_live_channel_get(ngx_str_t *id);

ngx_int_t ngx_live_channel_update(ngx_live_channel_t *channel,
    ngx_live_channel_conf_t *conf);

void ngx_live_channel_setup_changed(ngx_live_channel_t *channel);

void ngx_live_channel_ack_frames(ngx_live_channel_t *channel);


ngx_buf_chain_t *ngx_live_channel_copy_chains(ngx_live_channel_t *channel,
    ngx_buf_chain_t *src, size_t size, ngx_buf_chain_t **out_tail);

ngx_int_t ngx_live_channel_block_str_set(ngx_live_channel_t *channel,
    ngx_block_str_t *dest, ngx_str_t *src);

ngx_int_t ngx_live_channel_block_str_read(ngx_live_channel_t *channel,
    ngx_block_str_t *dest, ngx_mem_rstream_t *rs);


size_t ngx_live_channel_json_get_size(ngx_live_channel_t *obj);

u_char *ngx_live_channel_json_write(u_char *p, ngx_live_channel_t *obj);

size_t ngx_live_channels_json_get_size(void *obj);

u_char *ngx_live_channels_json_write(u_char *p, void *obj);

size_t ngx_live_channel_ids_json_get_size(void *obj);

u_char *ngx_live_channel_ids_json_write(u_char *p, void *obj);

/* variant */
ngx_int_t ngx_live_variant_create(ngx_live_channel_t *channel, ngx_str_t *id,
    ngx_live_variant_conf_t *conf, ngx_log_t *log,
    ngx_live_variant_t **result);

void ngx_live_variant_free(ngx_live_variant_t *variant);

ngx_live_variant_t *ngx_live_variant_get(ngx_live_channel_t *channel,
    ngx_str_t *id);

ngx_int_t ngx_live_variant_update(ngx_live_variant_t *variant,
    ngx_live_variant_conf_t *conf, ngx_log_t *log);

ngx_int_t ngx_live_variant_set_track(ngx_live_variant_t *variant,
    ngx_live_track_t *track, ngx_log_t *log);

ngx_int_t ngx_live_variant_set_tracks(ngx_live_variant_t *variant,
    ngx_live_track_t **tracks, ngx_log_t *log);

void ngx_live_variants_update_active(ngx_live_channel_t *channel);

ngx_flag_t ngx_live_variant_is_active_last(ngx_live_variant_t *variant,
    ngx_live_timeline_t *timeline);

uint32_t ngx_live_variant_is_active_any(ngx_live_variant_t *variant,
    ngx_live_timeline_t *timeline, uint32_t req_media_types);

size_t ngx_live_variants_json_get_size(ngx_live_channel_t *obj);

u_char *ngx_live_variants_json_write(u_char *p, ngx_live_channel_t *obj);

size_t ngx_live_variant_ids_json_get_size(ngx_live_channel_t *obj);

u_char *ngx_live_variant_ids_json_write(u_char *p, ngx_live_channel_t *obj);

/* track */
ngx_int_t ngx_live_track_create(ngx_live_channel_t *channel,
    ngx_str_t *id, uint32_t int_id, uint32_t media_type, ngx_log_t *log,
    ngx_live_track_t **result);

void ngx_live_track_free(ngx_live_track_t *track);

ngx_live_track_t *ngx_live_track_get(ngx_live_channel_t *channel,
    ngx_str_t *id);

ngx_live_track_t *ngx_live_track_get_by_int(ngx_live_channel_t *channel,
    uint32_t id);


size_t ngx_live_tracks_json_get_size(ngx_live_channel_t *obj);

u_char *ngx_live_tracks_json_write(u_char *p, ngx_live_channel_t *obj);

size_t ngx_live_track_ids_json_get_size(ngx_live_channel_t *obj);

u_char *ngx_live_track_ids_json_write(u_char *p, ngx_live_channel_t *obj);


extern ngx_str_t  ngx_live_track_type_names[];

extern ngx_str_t  ngx_live_variant_role_names[];

extern ngx_str_t  ngx_live_track_media_type_names[];

#endif /* _NGX_LIVE_CHANNEL_H_INCLUDED_ */
