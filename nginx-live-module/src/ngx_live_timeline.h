#ifndef _NGX_LIVE_TIMELINE_H_INCLUDED_
#define _NGX_LIVE_TIMELINE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_live_segment_list.h"
#include "ngx_live.h"


#define NGX_LIVE_TIMELINE_LAST_DURATIONS  (3)
#define NGX_LIVE_TIMELINE_MAX_ID_LEN      (32)


typedef enum {
    ngx_live_end_list_off,
    ngx_live_end_list_on,
    ngx_live_end_list_forced,

    ngx_live_end_list_count
} ngx_live_end_list_e;


typedef struct {
    int64_t                            start;
    int64_t                            end;
    int64_t                            period_gap;
    uint64_t                           max_duration;
    uint32_t                           max_segments;
    unsigned                           active:1;
    unsigned                           no_truncate:1;
} ngx_live_timeline_conf_t;


typedef struct {
    uint64_t                           max_duration;
    uint32_t                           max_segments;
    uint32_t                           expiry_threshold;
    uint32_t                           target_duration_segments;
    uint32_t                           end_list;
} ngx_live_timeline_manifest_conf_t;


typedef struct ngx_live_period_s  ngx_live_period_t;

struct ngx_live_period_s {
    ngx_rbtree_node_t                  node;          /* key = segment_index */
    ngx_queue_t                        queue;

    ngx_live_segment_iter_t            segment_iter;
    int64_t                            time;
    int64_t                            correction;
    uint64_t                           duration;
    uint32_t                           segment_count;
};


typedef struct {
    ngx_live_timeline_manifest_conf_t  conf;

    int64_t                            availability_start_time;
    ngx_live_period_t                  first_period;    /* queue.prev unused */
    int64_t                            first_period_initial_time;
    uint32_t                           first_period_initial_segment_index;
    uint32_t                           first_period_index;

    uint32_t                           target_duration;
    uint32_t                           target_duration_segments;

    uint32_t                           sequence;
    time_t                             last_modified;
    uint32_t                           last_durations
                                          [NGX_LIVE_TIMELINE_LAST_DURATIONS];

    /* volatile */
    ngx_queue_t                       *sentinel;
    uint64_t                           duration;
    uint32_t                           segment_count;
    uint32_t                           period_count;
} ngx_live_manifest_timeline_t;


struct ngx_live_timeline_s {
    ngx_str_node_t                     sn;
    uintptr_t                          id_escape;
    ngx_queue_t                        queue;
    u_char                             id_buf[NGX_LIVE_TIMELINE_MAX_ID_LEN];
    ngx_live_channel_t                *channel;
    uint32_t                           int_id;
    ngx_log_t                          log;

    ngx_rbtree_t                       rbtree;
    ngx_rbtree_node_t                  sentinel;
    ngx_queue_t                        periods;
    int64_t                            first_period_initial_time;
    int64_t                            last_time;   /* after correction */

    ngx_live_timeline_conf_t           conf;
    time_t                             last_segment_created;
    time_t                             last_accessed;
    ngx_str_t                          src_id;
    u_char                             src_id_buf
                                           [NGX_LIVE_TIMELINE_MAX_ID_LEN];

    ngx_live_manifest_timeline_t       manifest;

    uint64_t                           removed_duration;

    /* volatile */
    uint64_t                           duration;
    uint32_t                           segment_count;
    uint32_t                           period_count;
};


void ngx_live_timeline_conf_default(ngx_live_timeline_conf_t *conf,
    ngx_live_timeline_manifest_conf_t *manifest_conf);

ngx_int_t ngx_live_timeline_create(ngx_live_channel_t *channel, ngx_str_t *id,
    ngx_live_timeline_conf_t *conf,
    ngx_live_timeline_manifest_conf_t *manifest_conf, ngx_log_t *log,
    ngx_live_timeline_t **result);

void ngx_live_timeline_free(ngx_live_timeline_t *timeline);

ngx_live_timeline_t *ngx_live_timeline_get(ngx_live_channel_t *channel,
    ngx_str_t *id);

ngx_int_t ngx_live_timeline_update(ngx_live_timeline_t *timeline,
    ngx_live_timeline_conf_t *conf,
    ngx_live_timeline_manifest_conf_t *manifest_conf, ngx_log_t *log);

ngx_int_t ngx_live_timeline_get_time(ngx_live_timeline_t *timeline,
    uint32_t flags, ngx_log_t *log, int64_t *time);

ngx_int_t ngx_live_timeline_copy(ngx_live_timeline_t *dest,
    ngx_live_timeline_t *source, ngx_log_t *log);

uint32_t ngx_live_timeline_get_segment_info(ngx_live_timeline_t *timeline,
    uint32_t segment_index, uint32_t flags, int64_t *correction);

uint32_t ngx_live_timeline_index_to_sequence(ngx_live_timeline_t *timeline,
    uint32_t segment_index, ngx_flag_t *exists);

uint32_t ngx_live_timeline_sequence_to_index(ngx_live_timeline_t *timeline,
    uint32_t sequence);

ngx_flag_t ngx_live_timeline_is_expired(ngx_live_timeline_t *timeline);

/*
 * NGX_OK - segment added
 * NGX_DONE - no active timelines
 */
ngx_int_t ngx_live_timelines_add_segment(ngx_live_channel_t *channel,
    int64_t time, uint32_t segment_index, uint32_t duration,
    ngx_flag_t force_new_period);

ngx_int_t ngx_live_timelines_update_last_segment(ngx_live_channel_t *channel,
    uint32_t duration);

ngx_flag_t ngx_live_timelines_cleanup(ngx_live_channel_t *channel);

void ngx_live_timelines_truncate(ngx_live_channel_t *channel,
    uint32_t segment_index);

ngx_int_t ngx_live_timelines_get_segment_index(ngx_live_channel_t *channel,
    int64_t time, uint32_t *segment_index);

ngx_int_t ngx_live_timelines_get_segment_iter(ngx_live_channel_t *channel,
    ngx_live_segment_iter_t *iter, uint32_t segment_index, int64_t *start);

int64_t ngx_live_timelines_get_last_time(ngx_live_channel_t *channel);

ngx_flag_t ngx_live_timeline_serve_end_list(ngx_live_timeline_t *timeline,
    ngx_live_track_t *track, uint32_t max_index);


size_t ngx_live_timeline_json_get_size(ngx_live_timeline_t *timeline);

u_char *ngx_live_timeline_json_write(u_char *p, ngx_live_timeline_t *timeline);

size_t ngx_live_timeline_channel_json_get_size(ngx_live_channel_t *channel);

u_char *ngx_live_timeline_channel_json_write(u_char *p,
    ngx_live_channel_t *channel);

size_t ngx_live_timeline_ids_json_get_size(ngx_live_channel_t *obj);

u_char *ngx_live_timeline_ids_json_write(u_char *p, ngx_live_channel_t *obj);


extern ngx_str_t  ngx_live_end_list_names[];

#endif /* _NGX_LIVE_TIMELINE_H_INCLUDED_ */
