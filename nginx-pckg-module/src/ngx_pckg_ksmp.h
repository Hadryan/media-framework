#ifndef _NGX_PCKG_KSMP_H_INCLUDED_
#define _NGX_PCKG_KSMP_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_persist.h>
#include <ngx_ksmp.h>
#include "media/media_format.h"


#define NGX_INT32_HEX_LEN  (8)

#define NGX_PCKG_PERSIST_TYPE_MEDIA             (0x73746773)    /* sgts */

#define NGX_PCKG_KSMP_PARSE_FLAG_EXTRA_DATA     (0x01)
#define NGX_PCKG_KSMP_PARSE_FLAG_TRANSFER_CHAR  (0x02)
#define NGX_PCKG_KSMP_PARSE_FLAG_CODEC_NAME     (0x04)


typedef struct ngx_pckg_channel_s     ngx_pckg_channel_t;
typedef struct ngx_pckg_track_s       ngx_pckg_track_t;
typedef struct ngx_pckg_timeline_s    ngx_pckg_timeline_t;
typedef struct ngx_pckg_media_info_s  ngx_pckg_media_info_t;


#include "ngx_pckg_media_info.h"


enum {
    NGX_PCKG_KSMP_CTX_MAIN = 0,
    NGX_PCKG_KSMP_CTX_CHANNEL,
    NGX_PCKG_KSMP_CTX_TIMELINE,
    NGX_PCKG_KSMP_CTX_VARIANT,
    NGX_PCKG_KSMP_CTX_TRACK,
    NGX_PCKG_KSMP_CTX_MEDIA_INFO_QUEUE,
    NGX_PCKG_KSMP_CTX_TRACK_PARTS,
    NGX_PCKG_KSMP_CTX_RENDITION_REPORTS,
    NGX_PCKG_KSMP_CTX_SEGMENT,

    NGX_PCKG_KSMP_CTX_SGTS_MAIN,
    NGX_PCKG_KSMP_CTX_SGTS_SEGMENT,

    NGX_PCKG_KSMP_CTX_COUNT
};


typedef struct {
    ngx_str_t                      channel_id;
    ngx_str_t                      timeline_id;
    ngx_str_t                      variant_ids;
    uint32_t                       media_type_mask;
    uint32_t                       media_type_count;
    uint32_t                       segment_index;
    uint32_t                       max_segment_index;
    uint32_t                       part_index;
    uint32_t                       skip_boundary_percent;
    int64_t                        time;
    size_t                         padding;
    uint32_t                       flags;
    uint32_t                       parse_flags;
} ngx_pckg_ksmp_req_t;


typedef struct {
    ngx_pckg_timeline_t           *timeline;
    ngx_ksmp_period_header_t       header;
    ngx_ksmp_segment_repeat_t     *elts;
    ngx_uint_t                     nelts;
    ngx_uint_t                     segment_count;
    uint64_t                       duration;
} ngx_pckg_period_t;


struct ngx_pckg_timeline_s {
    ngx_pckg_channel_t            *channel;
    ngx_str_t                      id;
    ngx_ksmp_timeline_header_t     header;
    ngx_array_t                    periods;     /* ngx_pckg_period_t */
    ngx_uint_t                     segment_count;
    uint64_t                       duration;
    int64_t                        last_time;
    uint32_t                       last_segment;
    unsigned                       pending_segment:1;
};


typedef struct {
    ngx_ksmp_segment_info_elt_t   *elts;
    ngx_uint_t                     nelts;
} ngx_pckg_segment_info_t;


typedef struct {
    ngx_pckg_channel_t            *channel;
    ngx_pool_t                    *pool;
    ngx_ksmp_segment_header_t      header;
    ngx_ksmp_frame_t              *frames;
    ngx_str_t                      media;
} ngx_pckg_segment_t;


struct ngx_pckg_media_info_s {
    ngx_ksmp_media_info_header_t   header;
    kmp_media_info_t               kmp_media_info;
    media_info_t                   media_info;
    ngx_str_t                      extra_data;
    u_char                         codec_name[MAX_CODEC_NAME_SIZE];
};


typedef struct {
    uint32_t                       segment_index;
    uint32_t                       count;
    uint32_t                      *duration;
} ngx_pckg_segment_parts_t;


struct ngx_pckg_track_s {
    ngx_pckg_channel_t            *channel;
    ngx_ksmp_track_header_t        header;
    ngx_array_t                    media_info;  /* ngx_pckg_media_info_t */
    ngx_array_t                    parts;       /* ngx_pckg_segment_parts_t */
    ngx_pckg_segment_parts_t      *parts_cur;
    ngx_pckg_segment_parts_t      *parts_end;
    ngx_pckg_media_info_t         *last_media_info;
    ngx_pckg_media_info_iter_t     media_info_iter;
    ngx_pckg_segment_info_t        segment_info;
    ngx_pckg_segment_t            *segment;
    media_enc_t                   *enc;
};


typedef struct {
    ngx_pckg_channel_t            *channel;
    ngx_str_t                      id;
    ngx_ksmp_variant_t             header;
    ngx_str_t                      label;
    ngx_str_t                      lang;
    ngx_pckg_track_t              *tracks[KMP_MEDIA_COUNT];
    ngx_uint_t                     track_count;
} ngx_pckg_variant_t;


typedef struct {
    ngx_str_node_t                 sn;      /* must be first */
    ngx_str_t                      value;
} ngx_pckg_dynamic_var_t;


typedef struct {
    ngx_rbtree_t                   rbtree;
    ngx_rbtree_node_t              sentinel;
} ngx_pckg_dynamic_vars_t;


typedef struct {
    ngx_str_t                      variant_id;
    ngx_ksmp_rendition_report_t   *elts;
    ngx_uint_t                     nelts;
} ngx_pckg_rendition_report_t;


typedef struct {
    ngx_str_t                      id;
    ngx_str_t                      label;
    ngx_str_t                      lang;
    ngx_flag_t                     is_default;
} ngx_pckg_captions_service_t;


typedef struct {
    uint32_t                       track_id;
    uint32_t                       segment_index;
    uint32_t                       media_type_mask;

    uint32_t                       min_segment_index;
    uint32_t                       max_segment_index;
    uint32_t                       min_track_id;
    uint32_t                       max_track_id;
} ngx_pckg_channel_media_t;


struct ngx_pckg_channel_s {
    ngx_pool_t                    *pool;
    ngx_log_t                     *log;
    ngx_persist_conf_t            *persist;
    uint32_t                       format;

    uint32_t                       flags;
    uint32_t                       parse_flags;
    ngx_pckg_channel_media_t      *media;     /* sgts only */

    ngx_str_t                      id;
    ngx_ksmp_channel_header_t      header;
    ngx_pckg_timeline_t            timeline;
    ngx_array_t                    variants;  /* ngx_pckg_variant_t */
    ngx_array_t                    tracks;    /* ngx_pckg_track_t */
    ngx_array_t                    css;       /* ngx_pckg_captions_service_t */
    ngx_ksmp_segment_index_t      *segment_index;
    ngx_pckg_dynamic_vars_t        vars;
    uint32_t                       media_types;

    ngx_array_t                    rrs;       /* ngx_pckg_rendition_report_t */
    uint32_t                       rr_last_sequence;
    uint32_t                       rr_last_part_index;

    uint32_t                       err_code;
    ngx_str_t                      err_msg;
};


ngx_persist_conf_t *ngx_pckg_ksmp_conf_create(ngx_conf_t *cf);

ngx_int_t ngx_pckg_ksmp_create_request(ngx_pool_t *pool,
    ngx_pckg_ksmp_req_t *req, ngx_str_t *result);

ngx_int_t ngx_pckg_ksmp_parse_media_info(ngx_pckg_channel_t *channel,
    ngx_pckg_media_info_t *node);

ngx_int_t ngx_pckg_ksmp_parse(ngx_pckg_channel_t *channel, ngx_str_t *buf,
    size_t max_size);

#endif /* _NGX_PCKG_KSMP_H_INCLUDED_ */
