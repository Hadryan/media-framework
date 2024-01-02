
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include "ngx_stream_ts_module.h"

#include "ngx_ts_stream.h"


#define NGX_STREAM_TS_MAX_HEADER  4096


typedef struct {
    ngx_array_t                 *handlers;  /* ngx_ts_init_handler_t */
    ngx_stream_complex_value_t  *stream_id;

    ngx_msec_t                   timeout;
    size_t                       buffer_size;
    size_t                       mem_limit;
    ngx_str_t                    dump_folder;
} ngx_stream_ts_srv_conf_t;


typedef struct {
    ngx_ts_stream_t             *ts;
    u_char                      *buf;
} ngx_stream_ts_ctx_t;


static void ngx_stream_ts_handler(ngx_stream_session_t *s);
static void ngx_stream_ts_read_handler(ngx_event_t *rev);
static char *ngx_stream_ts(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_stream_ts_create_conf(ngx_conf_t *cf);
static char *ngx_stream_ts_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);


static ngx_command_t  ngx_stream_ts_commands[] = {

    { ngx_string("ts"),
      NGX_STREAM_SRV_CONF|NGX_CONF_NOARGS,
      ngx_stream_ts,
      0,
      0,
      NULL },

    { ngx_string("ts_stream_id"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_stream_set_complex_value_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_ts_srv_conf_t, stream_id),
      NULL },

    { ngx_string("ts_timeout"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_ts_srv_conf_t, timeout),
      NULL },

    { ngx_string("ts_buffer_size"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_ts_srv_conf_t, buffer_size),
      NULL },

    { ngx_string("ts_mem_limit"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_ts_srv_conf_t, mem_limit),
      NULL },

    { ngx_string("ts_dump_folder"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_ts_srv_conf_t, dump_folder),
      NULL },

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_ts_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    ngx_stream_ts_create_conf,     /* create server configuration */
    ngx_stream_ts_merge_conf       /* merge server configuration */
};


ngx_module_t  ngx_stream_ts_module = {
    NGX_MODULE_V1,
    &ngx_stream_ts_module_ctx,     /* module context */
    ngx_stream_ts_commands,        /* module directives */
    NGX_STREAM_MODULE,             /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


ngx_int_t
ngx_stream_ts_add_init_handler(ngx_conf_t *cf,
    ngx_ts_init_handler_pt handler, void *data)
{
    ngx_stream_ts_srv_conf_t  *tscf;

    tscf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_ts_module);

    return ngx_ts_add_init_handler(cf, &tscf->handlers, handler, data);
}


static void
ngx_stream_ts_handler(ngx_stream_session_t *s)
{
    ngx_chain_t                in;
    ngx_ts_stream_t           *ts;
    ngx_connection_t          *c;
    ngx_stream_ts_ctx_t       *ctx;
    ngx_ts_stream_conf_t       conf;
    ngx_stream_ts_srv_conf_t  *tscf;

    c = s->connection;

    ctx = ngx_pcalloc(c->pool, sizeof(ngx_stream_ts_ctx_t));
    if (ctx == NULL) {
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    tscf = ngx_stream_get_module_srv_conf(s, ngx_stream_ts_module);

    ctx->buf = ngx_pnalloc(c->pool, tscf->buffer_size);
    if (ctx->buf == NULL) {
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    if (tscf->stream_id) {
        if (ngx_stream_complex_value(s, tscf->stream_id, &conf.stream_id)
            != NGX_OK)
        {
            ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
            return;
        }

    } else {
        conf.stream_id.len = 0;
    }

    conf.mem_limit = tscf->mem_limit;
    conf.dump_folder = tscf->dump_folder;

    ts = ngx_ts_stream_create(c, c->pool, &conf);
    if (ts == NULL) {
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    ctx->ts = ts;

    ngx_stream_set_ctx(s, ctx, ngx_stream_ts_module);

    if (ngx_ts_init_handlers(tscf->handlers, ts) != NGX_OK) {
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    if (c->buffer && c->buffer->pos <= c->buffer->last) {
        ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0,
            "stream ts add preread buffer: %uz",
            c->buffer->last - c->buffer->pos);

        in.buf = c->buffer;
        in.next = NULL;

        if (ngx_ts_read(ts, &in) != NGX_OK) {
            ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    c->log->action = "reading data";

    c->read->handler = ngx_stream_ts_read_handler;

    ngx_stream_ts_read_handler(c->read);
}


static void
ngx_stream_ts_read_handler(ngx_event_t *rev)
{
    ssize_t                    n;
    ngx_buf_t                  b;
    ngx_chain_t                in;
    ngx_connection_t          *c;
    ngx_stream_ts_ctx_t       *ctx;
    ngx_stream_session_t      *s;
    ngx_stream_ts_srv_conf_t  *tscf;

    c = rev->data;
    s = c->data;

    if (ngx_exiting || ngx_terminate) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "ngx_stream_ts_read_handler: worker terminating");
        ngx_stream_finalize_session(s, NGX_STREAM_OK);
        return;
    }

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "ngx_stream_ts_read_handler: read timed out");
        ngx_stream_finalize_session(s, NGX_STREAM_OK);
        return;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_ts_module);
    tscf = ngx_stream_get_module_srv_conf(s, ngx_stream_ts_module);

    in.buf = &b;
    in.next = NULL;

    ngx_memzero(&b, sizeof(ngx_buf_t));

    while (rev->ready) {
        n = c->recv(c, ctx->buf, tscf->buffer_size);

        if (n == NGX_ERROR || n == 0) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "ngx_stream_ts_read_handler: recv returned %z", n);
            ngx_stream_finalize_session(s, NGX_STREAM_OK);
            return;
        }

        if (n == NGX_AGAIN) {
            break;
        }

        b.pos = ctx->buf;
        b.last = b.pos + n;

        if (ngx_ts_read(ctx->ts, &in) != NGX_OK) {
            ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    ngx_add_timer(rev, tscf->timeout);

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }
}


static char *
ngx_stream_ts(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_core_srv_conf_t  *cscf;

    cscf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_core_module);
    cscf->handler = ngx_stream_ts_handler;

    return NGX_CONF_OK;
}


static void *
ngx_stream_ts_create_conf(ngx_conf_t *cf)
{
    ngx_stream_ts_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_ts_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->handlers = NGX_CONF_UNSET_PTR;

    conf->timeout = NGX_CONF_UNSET_MSEC;
    conf->buffer_size = NGX_CONF_UNSET_SIZE;
    conf->mem_limit = NGX_CONF_UNSET_SIZE;

    return conf;
}


static char *
ngx_stream_ts_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_ts_srv_conf_t  *prev = parent;
    ngx_stream_ts_srv_conf_t  *conf = child;

    ngx_conf_merge_ptr_value(conf->handlers, prev->handlers, NULL);

    if (conf->stream_id == NULL) {
        conf->stream_id = prev->stream_id;
    }

    ngx_conf_merge_msec_value(conf->timeout, prev->timeout, 5000);
    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, 65536);
    ngx_conf_merge_size_value(conf->mem_limit, prev->mem_limit,
        5 * 1024 * 1024);
    ngx_conf_merge_str_value(conf->dump_folder, prev->dump_folder, "");

    return NGX_CONF_OK;
}
