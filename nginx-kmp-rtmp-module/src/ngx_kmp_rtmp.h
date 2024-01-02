#ifndef _NGX_KMP_RTMP_H_INCLUDED_
#define _NGX_KMP_RTMP_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <ngx_buf_chain.h>
#include <ngx_json_str.h>
#include <ngx_lba.h>


typedef struct ngx_kmp_rtmp_track_s     ngx_kmp_rtmp_track_t;
typedef struct ngx_kmp_rtmp_stream_s    ngx_kmp_rtmp_stream_t;
typedef struct ngx_kmp_rtmp_upstream_s  ngx_kmp_rtmp_upstream_t;


typedef struct {
    ngx_resolver_t  *resolver;
    ngx_msec_t       resolver_timeout;

    ngx_url_t       *notif_url;
    ngx_array_t     *notif_headers;
    ngx_msec_t       notif_timeout;
    ngx_msec_t       notif_read_timeout;
    size_t           notif_buffer_size;

    ngx_lba_t       *lba;
    size_t           mem_limit;
    ngx_uint_t       max_free_buffers;

    ngx_msec_t       timeout;
    ngx_msec_t       flush_timeout;

    ngx_str_t        flash_ver;
    size_t           chunk_size;
    ngx_msec_t       write_meta_timeout;
    ngx_msec_t       min_process_delay;
    ngx_msec_t       max_process_delay;
    ngx_msec_t       onfi_period;

    ngx_str_t        dump_folder;
} ngx_kmp_rtmp_upstream_conf_t;


ngx_int_t ngx_kmp_rtmp_init_process(ngx_cycle_t *cycle);

void ngx_kmp_rtmp_upstream_conf_init(ngx_kmp_rtmp_upstream_conf_t *conf);
ngx_int_t ngx_kmp_rtmp_upstream_conf_merge(ngx_conf_t *cf,
    ngx_kmp_rtmp_upstream_conf_t *prev, ngx_kmp_rtmp_upstream_conf_t *conf);

ngx_kmp_rtmp_upstream_t *ngx_kmp_rtmp_upstream_get(ngx_str_t *id);
void ngx_kmp_rtmp_upstream_free(ngx_kmp_rtmp_upstream_t *u, char *reason);

size_t ngx_kmp_rtmp_upstream_streams_json_get_size(
    ngx_kmp_rtmp_upstream_t *obj);
u_char *ngx_kmp_rtmp_upstream_streams_json_write(u_char *p,
    ngx_kmp_rtmp_upstream_t *obj);

size_t ngx_kmp_rtmp_upstream_stream_ids_json_get_size(
    ngx_kmp_rtmp_upstream_t *obj);
u_char *ngx_kmp_rtmp_upstream_stream_ids_json_write(u_char *p,
    ngx_kmp_rtmp_upstream_t *obj);

size_t ngx_kmp_rtmp_upstream_json_get_size(ngx_kmp_rtmp_upstream_t *obj);
u_char *ngx_kmp_rtmp_upstream_json_write(u_char *p,
    ngx_kmp_rtmp_upstream_t *obj);

size_t ngx_kmp_rtmp_upstreams_json_get_size(void *obj);
u_char *ngx_kmp_rtmp_upstreams_json_write(u_char *p, void *obj);

size_t ngx_kmp_rtmp_upstream_ids_json_get_size(void *obj);
u_char *ngx_kmp_rtmp_upstream_ids_json_write(u_char *p, void *obj);


extern ngx_json_str_t  ngx_kmp_rtmp_version;

#endif /* _NGX_KMP_RTMP_H_INCLUDED_ */
