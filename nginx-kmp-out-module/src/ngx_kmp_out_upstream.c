#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event_connect.h>

#include <ngx_json_parser.h>
#include <ngx_live_kmp.h>
#include <ngx_http_call.h>
#include <ngx_buf_queue_stream.h>

#include "ngx_kmp_out_utils.h"
#include "ngx_kmp_out_track_internal.h"
#include "ngx_kmp_out_upstream.h"


static void ngx_kmp_out_upstream_read_handler(ngx_event_t *rev);
static void ngx_kmp_out_upstream_write_handler(ngx_event_t *wev);


typedef struct {
    ngx_kmp_out_upstream_t  *u;
    ngx_uint_t               retries_left;
} ngx_kmp_out_republish_call_ctx_t;


static ngx_str_t  kmp_url_prefix = ngx_string("kmp://");

/* must match ngx_kmp_out_resume_from_xxx */
ngx_str_t  ngx_kmp_out_resume_from_names[] = {
    ngx_string("last_acked"),
    ngx_string("last_sent"),
    ngx_string("last_written"),
    ngx_null_string
};


#include "ngx_kmp_out_upstream_json.h"


static u_char *
ngx_kmp_out_upstream_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    u_char                  *p;
    ngx_kmp_out_upstream_t  *u;

    p = buf;

    if (log->action) {
        p = ngx_snprintf(buf, len, " while %s", log->action);
        len -= p - buf;
        buf = p;
    }

    u = log->data;
    if (u == NULL) {
        return p;
    }

    p = u->track->log.handler(&u->track->log, buf, len);
    len -= p - buf;
    buf = p;

    p = ngx_snprintf(buf, len, ", upstream: %V", &u->id.s);
    len -= p - buf;
    buf = p;

    if (u->peer.name) {
        p = ngx_snprintf(buf, len, ", peer: %V", u->peer.name);
        len -= p - buf;
        buf = p;
    }

    return p;
}


static ngx_int_t
ngx_kmp_out_upstream_realloc_buf(ngx_kmp_out_upstream_t *u, ngx_buf_t *b,
    size_t size)
{
    u_char  *p;
    size_t   alloc_size;

    alloc_size = b->end - b->start;
    if (alloc_size >= size) {
        return NGX_OK;
    }

    alloc_size *= 2;
    if (alloc_size < size) {
        alloc_size = size;
    }

    p = ngx_pnalloc(u->pool, alloc_size);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_realloc_buf: alloc failed %uz", alloc_size);
        return NGX_ERROR;
    }

    b->start = b->pos = b->last = p;
    b->end = p + alloc_size;

    return NGX_OK;
}


static ngx_int_t
ngx_kmp_out_upstream_copy(ngx_kmp_out_upstream_t *dst,
    ngx_kmp_out_upstream_t *src)
{
    size_t      size;
    ngx_buf_t  *src_mi, *dst_mi;

    src_mi = &src->acked_media_info;
    dst_mi = &dst->acked_media_info;

    size = src_mi->last - src_mi->pos;
    if (ngx_kmp_out_upstream_realloc_buf(dst, dst_mi, size) != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, &dst->log, 0,
            "ngx_kmp_out_upstream_copy: alloc failed");
        return NGX_ERROR;
    }

    dst_mi->last = ngx_copy(dst_mi->start, src_mi->pos, size);

    dst->acked_reader = src->acked_reader;
    dst->acked_frame_id = src->acked_frame_id;
    dst->acked_upstream_frame_id = src->acked_upstream_frame_id;
    dst->acked_offset = src->acked_offset;
    dst->acked_bytes = src->acked_bytes;

    return NGX_OK;
}


static ngx_kmp_out_upstream_t *
ngx_kmp_out_upstream_create(ngx_kmp_out_track_t *track, ngx_str_t *id,
    ngx_kmp_out_upstream_t *src)
{
    ngx_log_t               *log = &track->log;
    ngx_pool_t              *pool;
    ngx_kmp_out_upstream_t  *u;

    pool = ngx_create_pool(2048, log);
    if (pool == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, log, 0,
            "ngx_kmp_out_upstream_create: ngx_create_pool failed");
        return NULL;
    }

    u = ngx_palloc(pool, sizeof(ngx_kmp_out_upstream_t) + id->len);
    if (u == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, log, 0,
            "ngx_kmp_out_upstream_create: ngx_palloc failed");
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_memzero(u, sizeof(*u));

    u->id.s.data = (void *) (u + 1);

    u->pool = pool;
    u->log = *log;
    u->log.handler = ngx_kmp_out_upstream_log_error;
    u->log.data = u;
    u->log.action = NULL;

    u->id.s.len = id->len;
    ngx_memcpy(u->id.s.data, id->data, id->len);

    ngx_json_str_set_escape(&u->id);

    u->track = track;
    u->timeout = track->conf->timeout;

    if (src != NULL) {
        if (ngx_kmp_out_upstream_copy(u, src) != NGX_OK) {
            ngx_log_error(NGX_LOG_NOTICE, log, 0,
                "ngx_kmp_out_upstream_create: copy failed");
            ngx_destroy_pool(pool);
            return NULL;
        }

    } else {
        ngx_buf_queue_stream_init_head(&u->acked_reader, &track->buf_queue);
        u->acked_frame_id = track->connect.c.initial_frame_id;
        u->acked_upstream_frame_id =
            track->connect.c.initial_upstream_frame_id;
    }

    ngx_queue_insert_tail(&track->upstreams, &u->queue);

    ngx_log_error(NGX_LOG_INFO, &u->log, 0,
        "ngx_kmp_out_upstream_create: created %p", u);

    return u;
}


static ngx_int_t
ngx_kmp_out_upstream_set_connect_data(ngx_kmp_out_upstream_t *u,
    ngx_str_t *base64)
{
    ngx_buf_t  *cd;
    ngx_str_t   decoded;

    if (base64->data == NGX_JSON_UNSET_PTR) {
        return NGX_OK;
    }

    cd = &u->connect_data;

    if (ngx_kmp_out_upstream_realloc_buf(u, cd,
        ngx_base64_decoded_length(base64->len)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_set_connect_data: realloc failed");
        return NGX_ERROR;
    }

    decoded.data = cd->start;
    if (ngx_decode_base64(&decoded, base64) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, &u->log, 0,
            "ngx_kmp_out_upstream_set_connect_data: decode base64 failed");
        return NGX_ERROR;
    }

    cd->last = decoded.data + decoded.len;

    return NGX_OK;
}


static ngx_int_t
ngx_kmp_out_upstream_connect(ngx_kmp_out_upstream_t *u, ngx_addr_t *addr)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    if (addr->socklen > sizeof(u->sockaddr_buf)) {
        ngx_log_error(NGX_LOG_ALERT, &u->log, 0,
            "ngx_kmp_out_upstream_connect: address length %d too big",
            (int) addr->socklen);
        return NGX_ERROR;
    }

    u->remote_addr.s.data = u->remote_addr_buf;
    u->remote_addr.s.len = ngx_sock_ntop(addr->sockaddr, addr->socklen,
        u->remote_addr_buf, sizeof(u->remote_addr_buf), 1);

    ngx_json_str_set_escape(&u->remote_addr);

    u->peer.socklen = addr->socklen;
    u->peer.sockaddr = (void *) u->sockaddr_buf;
    ngx_memcpy(u->peer.sockaddr, addr->sockaddr, u->peer.socklen);

    u->peer.name = &u->remote_addr.s;
    u->peer.get = ngx_event_get_peer;
    u->peer.log = &u->log;
    u->peer.log_error = NGX_ERROR_ERR;

    rc = ngx_event_connect_peer(&u->peer);
    if (rc != NGX_OK && rc != NGX_AGAIN) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_connect: connect failed %i, addr: %V",
            rc, &u->remote_addr.s);
        return NGX_ERROR;
    }

    c = u->peer.connection;
    c->data = u;
    c->pool = u->pool;

    c->addr_text = u->remote_addr.s;

    c->log->connection = c->number;

    c->read->handler = ngx_kmp_out_upstream_read_handler;
    c->write->handler = ngx_kmp_out_upstream_write_handler;

    ngx_add_timer(c->write, u->timeout);

    u->recv_pos = (void *) &u->ack_frames;

    ngx_log_error(NGX_LOG_INFO, &u->log, 0,
        "ngx_kmp_out_upstream_connect: connecting to %V", &u->remote_addr.s);

    if (rc == NGX_OK) {
        ngx_kmp_out_upstream_write_handler(c->write);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_kmp_out_upstream_parse_url(ngx_pool_t *pool, ngx_str_t *url_str,
    ngx_url_t *url)
{
    if (url_str->data == NGX_JSON_UNSET_PTR) {
        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
            "ngx_kmp_out_upstream_parse_url: no upstream url in json");
        return NGX_ERROR;
    }

    if (url_str->len > kmp_url_prefix.len &&
        ngx_strncasecmp(url_str->data,
            kmp_url_prefix.data,
            kmp_url_prefix.len) == 0)
    {
        url_str->data += kmp_url_prefix.len;
        url_str->len -= kmp_url_prefix.len;
    }

    ngx_memzero(url, sizeof(*url));
    url->url = *url_str;
    url->no_resolve = 1;    /* accept only ips */

    if (ngx_parse_url(pool, url) != NGX_OK) {
        if (url->err) {
            ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                "ngx_kmp_out_upstream_parse_url: %s in \"%V\"",
                url->err, url_str);
        }
        return NGX_ERROR;
    }

    if (url->naddrs == 0) {
        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
            "ngx_kmp_out_upstream_parse_url: no addresses in \"%V\"",
            url_str);
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_kmp_out_upstream_free(ngx_kmp_out_upstream_t *u)
{
    ngx_pool_t  *pool = u->pool;

    ngx_log_error(NGX_LOG_INFO, &u->log, 0,
        "ngx_kmp_out_upstream_free: freeing %p", u);

    if (u->peer.connection) {
        ngx_close_connection(u->peer.connection);
    }

    /* republish_call is cancelled automatically since the pool is destroyed */

    if (u->republish.timer_set) {
        ngx_del_timer(&u->republish);
    }

    ngx_queue_remove(&u->queue);

    ngx_destroy_pool(pool);
}


static void
ngx_kmp_out_upstream_free_notify(ngx_kmp_out_upstream_t *u)
{
    ngx_flag_t            required;
    ngx_kmp_out_track_t  *track = u->track;

    required = u->required;

    ngx_kmp_out_upstream_free(u);

    if (!required && !ngx_queue_empty(&track->upstreams)) {
        return;
    }

    ngx_kmp_out_track_error(track, "upstream_error");
}


ngx_int_t
ngx_kmp_out_upstream_from_json(ngx_pool_t *temp_pool,
    ngx_kmp_out_track_t *track, ngx_kmp_out_upstream_t *src,
    ngx_json_object_t *obj)
{
    ngx_url_t                     url;
    ngx_kmp_out_upstream_t       *u;
    ngx_kmp_out_upstream_json_t   json;

    ngx_memset(&json, 0xff, sizeof(json));

    if (ngx_json_object_parse(temp_pool, obj, ngx_kmp_out_upstream_json,
            ngx_array_entries(ngx_kmp_out_upstream_json), &json)
        != NGX_JSON_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_from_json: failed to parse object");
        return NGX_ERROR;
    }

    if (ngx_kmp_out_upstream_parse_url(temp_pool, &json.url, &url) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_from_json: parse url failed");
        return NGX_ERROR;
    }

    if (json.id.data == NGX_JSON_UNSET_PTR) {
        json.id.len = 0;
    }

    u = ngx_kmp_out_upstream_create(track, &json.id, src);
    if (u == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_from_json: create failed");
        return NGX_ABORT;
    }

    if (ngx_kmp_out_upstream_set_connect_data(u, &json.connect_data)
        != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_from_json: set connect data failed");
        ngx_kmp_out_upstream_free(u);
        return NGX_ABORT;
    }

    if (ngx_kmp_out_upstream_connect(u, &url.addrs[0]) != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_from_json: connect failed");
        ngx_kmp_out_upstream_free(u);
        return NGX_ABORT;
    }

    u->required = json.required != 0;   /* enabled by default */
    ngx_json_set_uint_value(u->resume_from, json.resume_from);

    return NGX_OK;
}


static ngx_chain_t *
ngx_kmp_out_upstream_republish_create(void *arg, ngx_pool_t *pool,
    ngx_chain_t **body)
{
    u_char                            *p;
    size_t                             size;
    ngx_buf_t                         *b;
    ngx_chain_t                       *cl;
    ngx_kmp_out_track_t               *track;
    ngx_kmp_out_upstream_t            *u;
    ngx_kmp_out_track_conf_t          *conf;
    ngx_kmp_out_republish_call_ctx_t  *ctx = arg;

    u = ctx->u;
    track = u->track;

    size = sizeof("{,,}") +
        ngx_kmp_out_upstream_republish_json_get_size(u) +
        track->json_info.len +
        ngx_kmp_out_track_media_info_json_get_size(track);

    cl = ngx_http_call_alloc_chain_temp_buf(pool, size);
    if (cl == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, pool->log, 0,
            "ngx_kmp_out_upstream_republish_create: alloc chain buf failed");
        return NULL;
    }

    b = cl->buf;
    p = b->last;

    *p++ = '{';
    p = ngx_kmp_out_upstream_republish_json_write(p, u);
    if (track->json_info.len > 0) {
        *p++ = ',';
        p = ngx_copy(p, track->json_info.data, track->json_info.len);
    }

    *p++ = ',';
    p = ngx_kmp_out_track_media_info_json_write(p, track);
    *p++ = '}';

    if ((size_t) (p - b->pos) > size) {
        ngx_log_error(NGX_LOG_ALERT, pool->log, 0,
            "ngx_kmp_out_upstream_republish_create: "
            "result length %uz greater than allocated length %uz",
            (size_t) (b->last - b->pos), size);
        return NULL;
    }

    b->last = p;

    conf = track->conf;

    return ngx_http_call_format_json_post(pool,
        &conf->ctrl_republish_url->host, &conf->ctrl_republish_url->uri,
        conf->ctrl_headers, cl);
}


static ngx_int_t
ngx_kmp_out_upstream_republish_handle(ngx_pool_t *temp_pool, void *arg,
    ngx_uint_t code, ngx_str_t *content_type, ngx_buf_t *body)
{
    ngx_int_t                          rc;
    ngx_url_t                          url;
    ngx_str_t                          message;
    ngx_str_t                          code_str;
    ngx_json_value_t                   obj;
    ngx_kmp_out_upstream_t            *u;
    ngx_kmp_out_upstream_json_t        json;
    ngx_kmp_out_republish_call_ctx_t  *ctx = arg;

    u = ctx->u;

    if (ngx_kmp_out_parse_json_response(temp_pool, &u->log, code,
        content_type, body, &obj) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_republish_handle: parse response failed");
        goto retry;
    }

    rc = ngx_kmp_out_status_parse(temp_pool, &u->log, &obj.v.obj,
        &code_str, &message);
    switch (rc) {

    case NGX_OK:
        break;

    case NGX_DECLINED:
        ngx_log_error(NGX_LOG_ERR, &u->log, 0,
            "ngx_kmp_out_upstream_republish_handle: "
            "bad code \"%V\" in json, message=\"%V\"", &code_str, &message);
        goto retry;

    default:
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_republish_handle: failed to parse status");
        goto retry;
    }

    ngx_memset(&json, 0xff, sizeof(json));

    if (ngx_json_object_parse(temp_pool, &obj.v.obj,
            ngx_kmp_out_upstream_json,
            ngx_array_entries(ngx_kmp_out_upstream_json), &json)
        != NGX_JSON_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_republish_handle: failed to parse object");
        goto retry;
    }

    if (ngx_kmp_out_upstream_parse_url(temp_pool, &json.url, &url) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_republish_handle: parse url failed");
        goto retry;
    }

    if (ngx_kmp_out_upstream_set_connect_data(u, &json.connect_data)
        != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_republish_handle: set connect data failed");
        goto retry;
    }

    if (ngx_kmp_out_upstream_connect(u, &url.addrs[0]) != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, temp_pool->log, 0,
            "ngx_kmp_out_upstream_republish_handle: connect failed");
        ngx_kmp_out_upstream_free_notify(u);
        return NGX_OK;
    }

    u->republish_call = NULL;

    return NGX_OK;

retry:

    if (ctx->retries_left > 0) {
        ctx->retries_left--;
        return NGX_AGAIN;
    }

    ngx_kmp_out_upstream_free_notify(u);
    return NGX_OK;
}


static ngx_int_t
ngx_kmp_out_upstream_republish_send(ngx_kmp_out_upstream_t *u)
{
    ngx_url_t                         *url;
    ngx_http_call_init_t               ci;
    ngx_kmp_out_republish_call_ctx_t   ctx;

    /* send the request */
    url = u->track->conf->ctrl_republish_url;

    ctx.u = u;
    ctx.retries_left = u->track->conf->ctrl_retries;

    ngx_memzero(&ci, sizeof(ci));

    ci.url = url;
    ci.create = ngx_kmp_out_upstream_republish_create;
    ci.handle = ngx_kmp_out_upstream_republish_handle;
    ci.handler_pool = u->pool;
    ci.arg = &ctx;
    ci.argsize = sizeof(ctx);

    ngx_log_error(NGX_LOG_INFO, &u->log, 0,
        "ngx_kmp_out_upstream_republish_send: "
        "sending republish request to \"%V\"", &url->url);

    u->republish_call = ngx_kmp_out_track_http_call_create(u->track, &ci);
    if (u->republish_call == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_republish_send: "
            "failed to create http call");
        return NGX_ERROR;
    }

    u->republish_time = ngx_current_msec;

    return NGX_OK;
}


static void
ngx_kmp_out_upstream_republish_timer_handler(ngx_event_t *ev)
{
    ngx_kmp_out_upstream_t  *u = ev->data;

    if (ngx_kmp_out_upstream_republish_send(u) != NGX_OK) {
        ngx_kmp_out_upstream_free_notify(u);
    }
}


static ngx_int_t
ngx_kmp_out_upstream_republish(ngx_kmp_out_upstream_t *u)
{
    ngx_msec_t                 timer;
    ngx_msec_t                 elapsed;
    ngx_chain_t               *cl;
    ngx_kmp_out_track_t       *track;
    ngx_kmp_out_track_conf_t  *conf;

    track = u->track;
    if (track->state != NGX_KMP_TRACK_ACTIVE) {
        return NGX_DECLINED;
    }

    conf = track->conf;
    if (conf->ctrl_republish_url == NULL || u->no_republish) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_republish: cannot republish");
        return NGX_DECLINED;
    }

    elapsed = ngx_current_msec - u->republish_time;

    if (elapsed < conf->republish_interval
        && u->republishes >= conf->max_republishes)
    {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_republish: republishes limit reached");
        return NGX_DECLINED;
    }

    /* close the connection */
    u->log.connection = 0;
    ngx_close_connection(u->peer.connection);
    u->peer.connection = NULL;
    ngx_memzero(&u->remote_addr, sizeof(u->remote_addr));
    ngx_memzero(&u->local_addr, sizeof(u->local_addr));

    if (u->last) {
        u->last = NULL;

        if (!u->busy) {
            /* nothing to send, republish once more data is available */
            return NGX_OK;
        }

        for (cl = u->busy; cl->next; cl = cl->next);

        cl->next = u->free;
        u->free = u->busy;
        u->busy = NULL;
    }

    if (u->resume_from == ngx_kmp_out_resume_from_last_written) {
        if (ngx_kmp_out_upstream_auto_ack(u, NGX_MAX_SIZE_T_VALUE, 0) < 0) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_republish: auto ack failed");
            return NGX_ERROR;
        }
    }

    if (elapsed >= conf->republish_interval) {
        ngx_log_error(NGX_LOG_INFO, &u->log, 0,
            "ngx_kmp_out_upstream_republish: resetting republishes counter");

        u->republishes = 0;
        return ngx_kmp_out_upstream_republish_send(u);
    }

    timer = conf->republish_interval - elapsed;

    ngx_log_error(NGX_LOG_INFO, &u->log, 0,
        "ngx_kmp_out_upstream_republish: "
        "setting republish timer %M, republishes: %ui", timer, u->republishes);

    u->republishes++;

    u->republish.handler = ngx_kmp_out_upstream_republish_timer_handler;
    u->republish.data = u;
    u->republish.log = &u->log;

    ngx_add_timer(&u->republish, timer);

    return NGX_OK;
}


static void
ngx_kmp_out_upstream_error(ngx_kmp_out_upstream_t *u)
{
    ngx_uint_t  level;

    level = u->sent_end ? NGX_LOG_INFO : NGX_LOG_NOTICE;

    ngx_log_error(level, &u->log, 0,
        "ngx_kmp_out_upstream_error: called");

    if (ngx_kmp_out_upstream_republish(u) == NGX_OK) {
        return;
    }

    ngx_kmp_out_upstream_free_notify(u);
}


static ngx_int_t
ngx_kmp_out_upstream_ack_packet(ngx_kmp_out_upstream_t *u,
    kmp_packet_header_t *kmp_header)
{
    u_char     *p;
    size_t      size;
    ngx_buf_t  *mi;

    size = kmp_header->header_size + kmp_header->data_size;

    switch (kmp_header->packet_type) {

    case KMP_PACKET_MEDIA_INFO:
        mi = &u->acked_media_info;

        if (ngx_kmp_out_upstream_realloc_buf(u, mi, size) != NGX_OK) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_ack_packet: alloc failed");
            return NGX_ERROR;
        }

        p = mi->start;
        mi->last = p + size;

        p = ngx_copy(p, kmp_header, sizeof(*kmp_header));
        if (ngx_buf_queue_stream_read(&u->acked_reader, p,
            size - sizeof(*kmp_header)) == NULL)
        {
            ngx_log_error(NGX_LOG_ALERT, &u->log, 0,
                "ngx_kmp_out_upstream_ack_packet: "
                "failed to read media info packet, size: %uz", size);
            return NGX_ERROR;
        }

        ngx_log_error(NGX_LOG_INFO, &u->log, 0,
            "ngx_kmp_out_upstream_ack_packet: saved media info, size: %uz",
            size);
        break;

    case KMP_PACKET_FRAME:
        if (ngx_buf_queue_stream_skip(&u->acked_reader,
            size - sizeof(*kmp_header)) != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ALERT, &u->log, 0,
                "ngx_kmp_out_upstream_ack_packet: "
                "failed to skip frame packet, size: %uz", size);
            return NGX_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_KMP, &u->log, 0,
            "ngx_kmp_out_upstream_ack_packet: acked_frame_id: %uL, size: %uz",
            u->acked_frame_id, kmp_header->data_size);

        u->acked_frame_id++;
        break;

    case KMP_PACKET_NULL:
        break;

    case KMP_PACKET_END_OF_STREAM:      /* can happen in case of auto push */
        return NGX_DONE;

    default:
        ngx_log_error(NGX_LOG_ALERT, &u->log, 0,
            "ngx_kmp_out_upstream_ack_packet: "
            "invalid packet type 0x%uxD", kmp_header->packet_type);
        return NGX_ERROR;
    }

    u->acked_bytes += size;
    return NGX_OK;
}


static ngx_int_t
ngx_kmp_out_upstream_read_packet(ngx_kmp_out_upstream_t *u,
    kmp_packet_header_t *kmp_header, off_t limit)
{
    size_t                  size;
    ngx_buf_queue_stream_t  reader;

    if (u->acked_bytes + (off_t) sizeof(*kmp_header) > limit) {
        return NGX_DECLINED;
    }

    reader = u->acked_reader;
    if (ngx_buf_queue_stream_read(&u->acked_reader,
        kmp_header, sizeof(*kmp_header)) == NULL)
    {
        ngx_log_error(NGX_LOG_ALERT, &u->log, 0,
            "ngx_kmp_out_upstream_read_packet: read header failed");
        u->no_republish = 1;
        return NGX_ERROR;
    }

    size = kmp_header->header_size + kmp_header->data_size;
    if (u->acked_bytes + (off_t) size > limit) {
        u->acked_reader = reader;
        return NGX_DECLINED;
    }

    return NGX_OK;
}


ngx_int_t
ngx_kmp_out_upstream_auto_ack(ngx_kmp_out_upstream_t *u, size_t left,
    ngx_flag_t force)
{
    off_t                limit;
    size_t               size;
    ngx_int_t            rc;
    ngx_uint_t           count;
    kmp_packet_header_t  kmp_header;

    if (u->last) {
        limit = u->sent_base + u->peer.connection->sent;

    } else {
        limit = u->track->stats.last_frame_written;
    }

    ngx_log_debug4(NGX_LOG_DEBUG_KMP, &u->log, 0,
        "ngx_kmp_out_upstream_auto_ack: "
        "force: %i, connected: %d, acked_bytes: %O, limit: %O",
        force, u->last != NULL, u->acked_bytes, limit);

    for (count = 0; ; count++) {

        rc = ngx_kmp_out_upstream_read_packet(u, &kmp_header, limit);
        switch (rc) {

        case NGX_OK:
            break;

        case NGX_DECLINED:
            if (force && u->last) {
                ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                    "ngx_kmp_out_upstream_auto_ack: attempting republish");

                if (ngx_kmp_out_upstream_republish(u) == NGX_ERROR) {
                    return NGX_ERROR;
                }

                if (!u->last) {
                    limit = u->track->stats.last_frame_written;
                    continue;
                }
            }

            goto done;

        default:
            return NGX_ERROR;
        }

        rc = ngx_kmp_out_upstream_ack_packet(u, &kmp_header);
        if (rc != NGX_OK) {
            if (rc == NGX_DONE) {
                break;
            }

            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_auto_ack: ack packet failed");
            u->no_republish = 1;
            return NGX_ERROR;
        }

        if (kmp_header.packet_type == KMP_PACKET_FRAME) {
            u->auto_acked_frames++;
        }

        size = kmp_header.header_size + kmp_header.data_size;
        if (left <= size) {
            break;
        }

        left -= size;
    }

done:

    ngx_log_debug2(NGX_LOG_DEBUG_KMP, &u->log, 0,
        "ngx_kmp_out_upstream_auto_ack: "
        "acked %ui packets, acked_bytes: %O", count, u->acked_bytes);

    return count;
}


static ngx_int_t
ngx_kmp_out_upstream_ack_frames(ngx_kmp_out_upstream_t *u)
{
    off_t                sent;
    size_t               size;
    uint64_t             frame_id;
    ngx_int_t            rc;
    kmp_packet_header_t  kmp_header;

    sent = u->sent_base + u->peer.connection->sent;
    frame_id = u->ack_frames.a.frame_id;

    while (u->acked_frame_id < frame_id) {
        if (u->acked_bytes + (off_t) sizeof(kmp_header) > sent) {
            ngx_log_error(NGX_LOG_ERR, &u->log, 0,
                "ngx_kmp_out_upstream_ack_frames: packet header exceed sent");
            goto failed;
        }

        if (ngx_buf_queue_stream_read(&u->acked_reader,
            &kmp_header, sizeof(kmp_header)) == NULL)
        {
            ngx_log_error(NGX_LOG_ALERT, &u->log, 0,
                "ngx_kmp_out_upstream_ack_frames: read packet header failed");
            goto failed;
        }

        size = kmp_header.header_size + kmp_header.data_size;
        if (u->acked_bytes + (off_t) size > sent) {
            ngx_log_error(NGX_LOG_ERR, &u->log, 0,
                "ngx_kmp_out_upstream_ack_frames: packet data exceed sent");
            goto failed;
        }

        rc = ngx_kmp_out_upstream_ack_packet(u, &kmp_header);
        if (rc != NGX_OK) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_ack_frames: ack packet failed %i", rc);
            goto failed;
        }
    }

    u->acked_upstream_frame_id = u->ack_frames.a.upstream_frame_id;
    u->acked_offset = u->ack_frames.a.offset;

    return NGX_OK;

failed:

    u->no_republish = 1;
    return NGX_ERROR;
}


static ngx_int_t
ngx_kmp_out_upstream_parse_ack_packet(ngx_kmp_out_upstream_t *u)
{
    kmp_packet_header_t  *header = &u->ack_frames.header;

    if (header->packet_type != KMP_PACKET_ACK_FRAMES) {
        ngx_log_error(NGX_LOG_ERR, &u->log, 0,
            "ngx_kmp_out_upstream_parse_ack_packet: "
            "invalid packet type 0x%uxD", header->packet_type);
        return NGX_ERROR;
    }

    if (header->header_size != sizeof(u->ack_frames)) {
        ngx_log_error(NGX_LOG_ERR, &u->log, 0,
            "ngx_kmp_out_upstream_parse_ack_packet: "
            "invalid ack header size %uD", header->header_size);
        return NGX_ERROR;
    }

    if (header->data_size != 0) {
        ngx_log_error(NGX_LOG_ERR, &u->log, 0,
            "ngx_kmp_out_upstream_parse_ack_packet: "
            "invalid ack data size %uD", header->data_size);
        return NGX_ERROR;
    }

    if (u->resume_from != ngx_kmp_out_resume_from_last_acked) {
        ngx_log_error(NGX_LOG_WARN, &u->log, 0,
            "ngx_kmp_out_upstream_parse_ack_packet: "
            "unexpected ack packet received, resume_from: %d", u->resume_from);
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, &u->log, 0,
        "ngx_kmp_out_upstream_parse_ack_packet: "
        "got ack packet for frame %uL",
        u->ack_frames.a.frame_id);

    if (ngx_kmp_out_upstream_ack_frames(u) != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_parse_ack_packet: ack frames failed");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_kmp_out_upstream_read_handler(ngx_event_t *rev)
{
    u_char                  *recv_end;
    ssize_t                  n;
    ngx_uint_t               level;
    ngx_connection_t        *c;
    ngx_kmp_out_upstream_t  *u;

    c = rev->data;
    u = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_KMP, rev->log, 0,
        "ngx_kmp_out_upstream_read_handler: called");

    recv_end = (u_char *) &u->ack_frames + sizeof(u->ack_frames);

    for ( ;; ) {

        n = ngx_recv(c, u->recv_pos, recv_end - u->recv_pos);

        if (n > 0) {
            u->recv_pos += n;
            if (u->recv_pos < recv_end) {
                continue;
            }

            if (ngx_kmp_out_upstream_parse_ack_packet(u) != NGX_OK) {
                ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                    "ngx_kmp_out_upstream_read_handler: ack packet failed");
                break;
            }

            u->recv_pos = (u_char *) &u->ack_frames;
            continue;
        }

        if (n == NGX_AGAIN) {

            if (ngx_handle_read_event(rev, 0) != NGX_OK) {
                break;
            }

            return;
        }

        break;
    }

    level = u->sent_end ? NGX_LOG_INFO : NGX_LOG_NOTICE;
    ngx_log_error(level, &u->log, 0,
        "ngx_kmp_out_upstream_read_handler: upstream closed connection");
    ngx_kmp_out_upstream_error(u);
}


static ngx_int_t
ngx_kmp_out_upstream_send_chain(ngx_kmp_out_upstream_t *u)
{
    off_t              sent;
    ngx_chain_t       *chain;
    ngx_chain_t       *next;
    ngx_chain_t       *cl;
    ngx_connection_t  *c = u->peer.connection;

#if (NGX_DEBUG)
    size_t             buffered;
#endif

    if (!u->last) {
        /* connect packet not sent */
        return NGX_OK;
    }

    if (c->error) {
        return NGX_ERROR;
    }

    sent = c->sent;

    chain = c->send_chain(c, u->busy, 0);
    if (chain == NGX_CHAIN_ERROR) {
        ngx_log_error(NGX_LOG_NOTICE, c->log, 0,
            "ngx_kmp_out_upstream_send_chain: send_chain failed");
        c->error = 1;
        return NGX_ERROR;
    }

    /* move sent buffers to free */
    for (cl = u->busy; cl && cl != chain; cl = next) {
        next = cl->next;

        cl->next = u->free;
        u->free = cl;
    }

    if (u->resume_from != ngx_kmp_out_resume_from_last_acked) {
        if (ngx_kmp_out_upstream_auto_ack(u, NGX_MAX_SIZE_T_VALUE, 0) < 0) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_send_chain: auto ack failed");
            return NGX_ERROR;
        }
    }

    u->busy = chain;

#if (NGX_DEBUG)
    buffered = 0;
    for (cl = u->busy; cl; cl = cl->next) {
        buffered += cl->buf->last - cl->buf->pos;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_KMP, c->log, 0,
        "ngx_kmp_out_upstream_send_chain: sent %O bytes, %uz in buffer",
        c->sent - sent,
        buffered);
#endif

    if (u->busy == NULL) {

        if (c->write->timer_set) {
            ngx_del_timer(c->write);
        }

        u->last = &u->busy;

        if (!c->buffered) {
            return NGX_OK;
        }

    } else if (c->sent != sent) {
        ngx_add_timer(c->write, u->timeout);
    }

    return NGX_AGAIN;
}


ngx_int_t
ngx_kmp_out_upstream_send(ngx_kmp_out_upstream_t *u)
{
    ngx_int_t  rc;

    if (u->peer.connection == NULL) {
        /* republish in progress */
        return NGX_AGAIN;
    }

    rc = ngx_kmp_out_upstream_send_chain(u);
    if (rc != NGX_ERROR) {
        return rc;
    }

    if (ngx_kmp_out_upstream_republish(u) == NGX_OK) {
        return NGX_AGAIN;
    }

    return NGX_ERROR;
}


static ngx_int_t
ngx_kmp_out_upstream_send_buffered(ngx_kmp_out_upstream_t *u)
{
    u_char                *end;
    u_char                *start;
    ngx_pool_t            *pool = u->pool;
    ngx_chain_t           *cl;
    ngx_connection_t      *c;
    ngx_buf_queue_node_t  *cur;

    c = u->peer.connection;
    u->sent_base = u->acked_bytes - c->sent;

    u->last = &u->busy;

    u->local_addr.s.len = NGX_SOCKADDR_STRLEN;
    u->local_addr.s.data = u->local_addr_buf;

    if (ngx_connection_local_sockaddr(c, &u->local_addr.s, 1) != NGX_OK) {
        u->local_addr.s.len = 0;
    }

    ngx_json_str_set_escape(&u->local_addr);

    /* connect header */
    u->connect = u->track->connect;
    u->connect.header.data_size = u->connect_data.last - u->connect_data.start;
    u->connect.c.initial_frame_id = u->acked_frame_id;
    u->connect.c.initial_upstream_frame_id = u->acked_upstream_frame_id;
    u->connect.c.initial_offset = u->acked_offset;
    cl = ngx_kmp_out_alloc_chain_buf(pool, &u->connect, &u->connect + 1);
    if (cl == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_send_buffered: alloc chain buf failed");
        return NGX_ERROR;
    }

    *u->last = cl;
    u->last = &cl->next;

    u->sent_base -= sizeof(u->connect);

    /* connect data */
    if (u->connect.header.data_size) {
        cl = ngx_kmp_out_alloc_chain_buf(pool, u->connect_data.start,
            u->connect_data.last);
        if (cl == NULL) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_send_buffered: alloc chain buf failed");
            return NGX_ERROR;
        }

        *u->last = cl;
        u->last = &cl->next;

        u->sent_base -= u->connect.header.data_size;
    }

    /* initial media info */
    if (u->acked_media_info.last > u->acked_media_info.start) {
        cl = ngx_kmp_out_alloc_chain_buf(pool, u->acked_media_info.start,
            u->acked_media_info.last);
        if (cl == NULL) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_send_buffered: alloc chain buf failed");
            return NGX_ERROR;
        }

        *u->last = cl;
        u->last = &cl->next;

        u->sent_base -= (u->acked_media_info.last - u->acked_media_info.start);
    }

    /* media info / frames */
    for (cur = u->acked_reader.node; cur; cur = ngx_buf_queue_next(cur)) {

        start = ngx_buf_queue_start(cur);

        if (start == u->track->active_buf.start) {
            end = u->track->active_buf.pos;

        } else {
            end = ngx_buf_queue_end(&u->track->buf_queue, cur);
        }

        if (cur == u->acked_reader.node) {
            start = ngx_buf_queue_stream_pos(&u->acked_reader);
        }

        if (start >= end) {
            continue;
        }

        cl = ngx_kmp_out_alloc_chain_buf(pool, start, end);
        if (cl == NULL) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_send_buffered: alloc chain buf failed");
            return NGX_ERROR;
        }

        *u->last = cl;
        u->last = &cl->next;
    }

    *u->last = NULL;

    return NGX_OK;
}


static void
ngx_kmp_out_upstream_write_handler(ngx_event_t *wev)
{
    ngx_int_t                rc;
    ngx_connection_t        *c;
    ngx_kmp_out_upstream_t  *u;

    c = wev->data;
    u = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_KMP, wev->log, 0,
        "ngx_kmp_out_upstream_write_handler: called");

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, NGX_ETIMEDOUT,
            "ngx_kmp_out_upstream_write_handler: write timed out");
        goto failed;
    }

    if (!u->last) {
        if (ngx_kmp_out_upstream_send_buffered(u) != NGX_OK) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_write_handler: send buffered failed");
            goto failed;
        }
    }

    rc = ngx_kmp_out_upstream_send_chain(u);
    if (rc != NGX_OK && rc != NGX_AGAIN) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_write_handler: send chain failed");
        goto failed;
    }

    if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
            "ngx_kmp_out_upstream_write_handler: "
            "ngx_handle_write_event failed");
        goto failed;
    }

    return;

failed:

    ngx_kmp_out_upstream_error(u);
}


ngx_int_t
ngx_kmp_out_upstream_append_buffer(ngx_kmp_out_upstream_t *u,
    ngx_buf_t *active_buf)
{
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    if (!u->last) {

        if (u->resume_from == ngx_kmp_out_resume_from_last_written) {
            if (ngx_kmp_out_upstream_auto_ack(u, NGX_MAX_SIZE_T_VALUE, 0)
                < 0)
            {
                ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                    "ngx_kmp_out_upstream_append_buffer: auto ack failed");
                return NGX_ERROR;
            }
        }

        /* connect packet not sent */
        if (u->peer.connection == NULL &&
            u->republish_call == NULL &&
            !u->republish.timer_set)
        {
            /* start a republish */
            return ngx_kmp_out_upstream_republish_send(u);
        }

        return NGX_OK;
    }

    cl = u->free;
    if (cl != NULL) {
        u->free = cl->next;
        b = cl->buf;

        b->start = b->pos = active_buf->pos;
        b->end = b->last = active_buf->last;

    } else {
        cl = ngx_kmp_out_alloc_chain_buf(u->pool, active_buf->pos,
            active_buf->last);
        if (cl == NULL) {
            ngx_log_error(NGX_LOG_NOTICE, &u->log, 0,
                "ngx_kmp_out_upstream_append_buffer: alloc chain buf failed");
            return NGX_ERROR;
        }
    }

    cl->next = NULL;
    *u->last = cl;
    u->last = &cl->next;

    return NGX_OK;
}
