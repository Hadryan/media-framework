#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_http_pckg_utils.h"


static ngx_int_t  ngx_http_pckg_error_map[VOD_ERROR_LAST - VOD_ERROR_FIRST] = {
    NGX_HTTP_NOT_FOUND,                 /* VOD_BAD_DATA             */
    NGX_HTTP_INTERNAL_SERVER_ERROR,     /* VOD_ALLOC_FAILED         */
    NGX_HTTP_INTERNAL_SERVER_ERROR,     /* VOD_UNEXPECTED           */
    NGX_HTTP_BAD_REQUEST,               /* VOD_BAD_REQUEST          */
    NGX_HTTP_SERVICE_UNAVAILABLE,       /* VOD_BAD_MAPPING          */
    NGX_HTTP_NOT_FOUND,                 /* VOD_EXPIRED              */
    NGX_HTTP_NOT_FOUND,                 /* VOD_NO_STREAMS           */
    NGX_HTTP_NOT_FOUND,                 /* VOD_EMPTY_MAPPING        */
    NGX_HTTP_INTERNAL_SERVER_ERROR,     /* VOD_NOT_FOUND (internal) */
    NGX_HTTP_INTERNAL_SERVER_ERROR,     /* VOD_REDIRECT (internal)  */
};


#if nginx_version < 1021000
char *
ngx_http_set_complex_value_zero_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    char  *p = conf;

    ngx_str_t                          *value;
    ngx_http_complex_value_t          **cv;
    ngx_http_compile_complex_value_t    ccv;

    cv = (ngx_http_complex_value_t **) (p + cmd->offset);

    if (*cv != NGX_CONF_UNSET_PTR) {
        return "is duplicate";
    }

    *cv = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (*cv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = *cv;
    ccv.zero = 1;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
#endif


static ngx_flag_t
ngx_parse_flag(ngx_str_t *value)
{
    if (value->len == 2 && ngx_strncasecmp(value->data,
        (u_char *) "on", 2) == 0)
    {
        return 1;

    } else if (value->len == 3 && ngx_strncasecmp(value->data,
        (u_char *) "off", 3) == 0)
    {
        return 0;

    } else {
        return NGX_ERROR;
    }
}


ngx_flag_t
ngx_http_complex_value_flag(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_flag_t default_value)
{
    ngx_str_t   value;
    ngx_flag_t  flag;

    if (val == NULL) {
        return default_value;
    }

    if (val->lengths == NULL) {
        return val->u.size;
    }

    if (ngx_http_complex_value(r, val, &value) != NGX_OK) {
        return default_value;
    }

    flag = ngx_parse_flag(&value);

    if (flag == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "invalid flag \"%V\"", &value);
        return default_value;
    }

    return flag;
}


char *
ngx_http_set_complex_value_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    char  *p = conf;

    char                      *rv;
    ngx_http_complex_value_t  *cv;

    rv = ngx_http_set_complex_value_slot(cf, cmd, conf);

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    cv = *(ngx_http_complex_value_t **) (p + cmd->offset);

    if (cv->lengths) {
        return NGX_CONF_OK;
    }

    cv->u.size = ngx_parse_flag(&cv->value);
    if (cv->u.size == (size_t) NGX_ERROR) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


ngx_uint_t
ngx_http_complex_value_percent(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_uint_t default_value)
{
    ngx_str_t  value;
    ngx_int_t  percent;

    if (val == NULL) {
        return default_value;
    }

    if (val->lengths == NULL) {
        return val->u.size;
    }

    if (ngx_http_complex_value(r, val, &value) != NGX_OK) {
        return default_value;
    }

    percent = ngx_atofp(value.data, value.len, 2);
    if (percent == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "invalid value \"%V\"", &value);
        return default_value;
    }

    return percent;
}


char *
ngx_http_set_complex_value_percent_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    char  *p = conf;

    char                      *rv;
    ngx_http_complex_value_t  *cv;

    rv = ngx_http_set_complex_value_slot(cf, cmd, conf);

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    cv = *(ngx_http_complex_value_t **) (p + cmd->offset);

    if (cv->lengths) {
        return NGX_CONF_OK;
    }

    cv->u.size = ngx_atofp(cv->value.data, cv->value.len, 2);
    if (cv->u.size == (size_t) NGX_ERROR) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


/* uri parsing */

u_char *
ngx_http_pckg_parse_uint32(u_char *start_pos, u_char *end_pos,
    uint32_t *result)
{
    uint32_t  value = 0;

    for (;
        start_pos < end_pos && *start_pos >= '0' && *start_pos <= '9';
        start_pos++)
    {
        value = value * 10 + *start_pos - '0';
    }

    *result = value;
    return start_pos;
}


u_char *
ngx_http_pckg_extract_string(u_char *start_pos, u_char *end_pos,
    ngx_str_t *result)
{
    result->data = start_pos;
    start_pos = ngx_strlchr(start_pos, end_pos, '-');
    if (start_pos == NULL) {
        start_pos = end_pos;
    }

    result->len = start_pos - result->data;
    return start_pos;
}


#define expect_char(start_pos, end_pos, ch)                                  \
    if (start_pos >= end_pos || *start_pos != ch) {                          \
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,                    \
            "ngx_http_pckg_parse_uri_file_name: "                            \
            "expected \"%c\"", ch);                                          \
        return NGX_HTTP_BAD_REQUEST;                                         \
    }                                                                        \
    start_pos++;


ngx_int_t
ngx_http_pckg_parse_uri_file_name(ngx_http_request_t *r,
    u_char *start_pos, u_char *end_pos, uint32_t flags,
    ngx_pckg_ksmp_req_t *result)
{
    u_char     *p;
    uint32_t    media_type_flag;
    uint32_t    media_type_mask;
    ngx_str_t   cur;

    /* required params */

    if (flags & NGX_HTTP_PCKG_PARSE_REQUIRE_INDEX) {
        expect_char(start_pos, end_pos, '-');

        start_pos = ngx_http_pckg_parse_uint32(start_pos, end_pos,
            &result->segment_index);
        if (result->segment_index <= 0) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_pckg_parse_uri_file_name: "
                "failed to parse segment index");
            return NGX_HTTP_BAD_REQUEST;
        }

        result->segment_index--;
    }

    if (flags & NGX_HTTP_PCKG_PARSE_REQUIRE_PART_INDEX) {
        expect_char(start_pos, end_pos, '-');

        start_pos = ngx_http_pckg_parse_uint32(start_pos, end_pos,
            &result->part_index);
        if (result->part_index <= 0) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_pckg_parse_uri_file_name: "
                "failed to parse part index");
            return NGX_HTTP_BAD_REQUEST;
        }

        result->part_index--;
    }

    if (flags & NGX_HTTP_PCKG_PARSE_REQUIRE_SINGLE_VARIANT) {
        expect_char(start_pos, end_pos, '-');
        expect_char(start_pos, end_pos, 's');

        start_pos = ngx_http_pckg_extract_string(start_pos, end_pos,
            &result->variant_ids);
    }

    /* optional params */

    if (start_pos >= end_pos) {
        return NGX_OK;
    }

    if (*start_pos != '-' || end_pos - start_pos < 2) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "ngx_http_pckg_parse_uri_file_name: "
            "expected \"-\" followed by a specifier");
        return NGX_HTTP_BAD_REQUEST;
    }

    start_pos++;    /* skip the - */

    if (*start_pos == 's' &&
        (flags & (NGX_HTTP_PCKG_PARSE_OPTIONAL_VARIANTS
         | NGX_HTTP_PCKG_PARSE_OPTIONAL_SINGLE_VARIANT)))
    {
        p = ngx_pnalloc(r->pool, end_pos - start_pos);
        if (p == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "ngx_http_pckg_parse_uri_file_name: alloc failed");
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        result->variant_ids.data = p;

        do {

            start_pos++;    /* skip the s */

            if (p > result->variant_ids.data) {
                *p++ = NGX_KSMP_VARIANT_IDS_DELIM;
            }

            start_pos = ngx_http_pckg_extract_string(start_pos, end_pos, &cur);

            p = ngx_copy_str(p, cur);

            if (start_pos >= end_pos) {
                result->variant_ids.len = p - result->variant_ids.data;
                return NGX_OK;
            }

            if (*start_pos != '-' || end_pos - start_pos < 2) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "ngx_http_pckg_parse_uri_file_name: "
                    "expected \"-\" followed by a specifier");
                return NGX_HTTP_BAD_REQUEST;
            }

            start_pos++;    /* skip the - */

        } while (*start_pos == 's');

        result->variant_ids.len = p - result->variant_ids.data;

        if ((flags & NGX_HTTP_PCKG_PARSE_OPTIONAL_SINGLE_VARIANT)
            && ngx_strlchr(result->variant_ids.data, p,
                NGX_KSMP_VARIANT_IDS_DELIM) != NULL)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_pckg_parse_uri_file_name: "
                "invalid variant id \"%V\"", &result->variant_ids);
            return NGX_HTTP_BAD_REQUEST;
        }
    }

    if ((*start_pos == 'v' || *start_pos == 'a' || *start_pos == 't') &&
        (flags & NGX_HTTP_PCKG_PARSE_OPTIONAL_MEDIA_TYPE))
    {
        media_type_mask = result->media_type_mask;

        result->media_type_mask = 0;
        result->media_type_count = 0;

        while (*start_pos != '-') {

            switch (*start_pos) {

            case 'v':
                media_type_flag = 1 << KMP_MEDIA_VIDEO;
                break;

            case 'a':
                media_type_flag = 1 << KMP_MEDIA_AUDIO;
                break;

            case 't':
                media_type_flag = 1 << KMP_MEDIA_SUBTITLE;
                break;

            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "ngx_http_pckg_parse_uri_file_name: "
                    "invalid media type \"%c\"", *start_pos);
                return NGX_HTTP_BAD_REQUEST;
            }

            if (!(media_type_mask & media_type_flag)) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "ngx_http_pckg_parse_uri_file_name: "
                    "unsupported media type \"%c\"", *start_pos);
                return NGX_HTTP_BAD_REQUEST;
            }

            if (result->media_type_mask & media_type_flag) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "ngx_http_pckg_parse_uri_file_name: "
                    "media type \"%c\" repeats more than once", *start_pos);
                return NGX_HTTP_BAD_REQUEST;
            }

            result->media_type_mask |= media_type_flag;
            result->media_type_count++;

            start_pos++;

            if (start_pos >= end_pos) {
                return NGX_OK;
            }
        }

        start_pos++;    /* skip the - */

        if (start_pos >= end_pos) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "ngx_http_pckg_parse_uri_file_name: "
                "trailing dash in file name");
            return NGX_HTTP_BAD_REQUEST;
        }
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
        "ngx_http_pckg_parse_uri_file_name: "
        "did not consume the whole name");
    return NGX_HTTP_BAD_REQUEST;
}


ngx_int_t
ngx_http_pckg_complex_value_json(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_json_value_t *json)
{
    ngx_int_t  rc;
    ngx_str_t  str;
    u_char     error[128];

    if (ngx_http_complex_value(r, val, &str) != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
            "ngx_http_pckg_complex_value_json: complex value failed");
        return NGX_ERROR;
    }

    rc =  ngx_json_parse(r->pool, str.data, json, error, sizeof(error));
    if (rc != NGX_JSON_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "ngx_http_pckg_complex_value_json: ngx_json_parse failed %i, %s",
            rc, error);
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* Implemented according to nginx's ngx_http_range_parse,
    dropped multi range support */

ngx_int_t
ngx_http_pckg_range_parse(ngx_str_t *range, off_t content_length,
    off_t *out_start, off_t *out_end)
{
    u_char            *p;
    off_t              start, end, cutoff, cutlim;
    ngx_uint_t         suffix;

    if (range->len < 7
        || ngx_strncasecmp(range->data,
                        (u_char *) "bytes=", 6) != 0)
    {
        return NGX_HTTP_RANGE_NOT_SATISFIABLE;
    }

    p = range->data + 6;

    cutoff = NGX_MAX_OFF_T_VALUE / 10;
    cutlim = NGX_MAX_OFF_T_VALUE % 10;

    start = 0;
    end = 0;
    suffix = 0;

    while (*p == ' ') { p++; }

    if (*p != '-') {
        if (*p < '0' || *p > '9') {
            return NGX_HTTP_RANGE_NOT_SATISFIABLE;
        }

        while (*p >= '0' && *p <= '9') {
            if (start >= cutoff && (start > cutoff || *p - '0' > cutlim)) {
                return NGX_HTTP_RANGE_NOT_SATISFIABLE;
            }

            start = start * 10 + *p++ - '0';
        }

        while (*p == ' ') { p++; }

        if (*p++ != '-') {
            return NGX_HTTP_RANGE_NOT_SATISFIABLE;
        }

        while (*p == ' ') { p++; }

        if (*p == '\0') {
            end = content_length;
            goto found;
        }

    } else {
        suffix = 1;
        p++;
    }

    if (*p < '0' || *p > '9') {
        return NGX_HTTP_RANGE_NOT_SATISFIABLE;
    }

    while (*p >= '0' && *p <= '9') {
        if (end >= cutoff && (end > cutoff || *p - '0' > cutlim)) {
            return NGX_HTTP_RANGE_NOT_SATISFIABLE;
        }

        end = end * 10 + *p++ - '0';
    }

    while (*p == ' ') { p++; }

    if (*p != '\0') {
        return NGX_HTTP_RANGE_NOT_SATISFIABLE;
    }

    if (suffix) {
        start = content_length - end;
        end = content_length - 1;
    }

    if (end >= content_length) {
        end = content_length;

    } else {
        end++;
    }

found:

    if (start >= end) {
        return NGX_HTTP_RANGE_NOT_SATISFIABLE;
    }

    *out_start = start;
    *out_end = end;

    return NGX_OK;
}


/* response headers */

#if (nginx_version >= 1023000)
static ngx_table_elt_t *
ngx_http_pckg_push_cache_control(ngx_http_request_t *r)
{
    ngx_table_elt_t  *cc;

    cc = r->headers_out.cache_control;

    if (cc == NULL) {

        cc = ngx_list_push(&r->headers_out.headers);
        if (cc == NULL) {
            return NULL;
        }

        r->headers_out.cache_control = cc;
        cc->next = NULL;

        cc->hash = 1;
        ngx_str_set(&cc->key, "Cache-Control");

    } else {
        for (cc = cc->next; cc; cc = cc->next) {
            cc->hash = 0;
        }

        cc = r->headers_out.cache_control;
        cc->next = NULL;
    }

    return cc;
}
#else
static ngx_table_elt_t *
ngx_http_pckg_push_cache_control(ngx_http_request_t *r)
{
    ngx_uint_t        i;
    ngx_table_elt_t  *cc, **ccp;

    ccp = r->headers_out.cache_control.elts;

    if (ccp == NULL) {

        if (ngx_array_init(&r->headers_out.cache_control, r->pool,
                           1, sizeof(ngx_table_elt_t *))
            != NGX_OK)
        {
            return NULL;
        }

        ccp = ngx_array_push(&r->headers_out.cache_control);
        if (ccp == NULL) {
            return NULL;
        }

        cc = ngx_list_push(&r->headers_out.headers);
        if (cc == NULL) {
            return NULL;
        }

        cc->hash = 1;
        ngx_str_set(&cc->key, "Cache-Control");
        *ccp = cc;

    } else {
        for (i = 1; i < r->headers_out.cache_control.nelts; i++) {
            ccp[i]->hash = 0;
        }

        cc = ccp[0];
    }

    return cc;
}
#endif

/* A run down version of ngx_http_set_expires */
static ngx_int_t
ngx_http_pckg_set_expires(ngx_http_request_t *r, time_t expires_time)
{
    size_t            len;
    time_t            now, max_age;
    ngx_table_elt_t  *e, *cc;

    e = r->headers_out.expires;

    if (e == NULL) {

        e = ngx_list_push(&r->headers_out.headers);
        if (e == NULL) {
            return NGX_ERROR;
        }

        r->headers_out.expires = e;
#if (nginx_version >= 1023000)
        e->next = NULL;
#endif

        e->hash = 1;
        ngx_str_set(&e->key, "Expires");
    }

    len = sizeof("Mon, 28 Sep 1970 06:00:00 GMT");
    e->value.len = len - 1;

    cc = ngx_http_pckg_push_cache_control(r);
    if (cc == NULL) {
        e->hash = 0;
        return NGX_ERROR;
    }

    e->value.data = ngx_pnalloc(r->pool, len);
    if (e->value.data == NULL) {
        e->hash = 0;
        cc->hash = 0;
        return NGX_ERROR;
    }

    if (expires_time == 0) {
        ngx_memcpy(e->value.data, ngx_cached_http_time.data,
            ngx_cached_http_time.len + 1);
        ngx_str_set(&cc->value, "max-age=0");
        return NGX_OK;
    }

    now = ngx_time();

    max_age = expires_time;
    expires_time += now;

    ngx_http_time(e->value.data, expires_time);

    if (max_age < 0) {
        ngx_str_set(&cc->value, "no-cache");
        return NGX_OK;
    }

    cc->value.data = ngx_pnalloc(r->pool,
        sizeof("max-age=") + NGX_TIME_T_LEN + 1);
    if (cc->value.data == NULL) {
        cc->hash = 0;
        return NGX_ERROR;
    }

    cc->value.len = ngx_sprintf(cc->value.data, "max-age=%T", max_age)
        - cc->value.data;

    return NGX_OK;
}


ngx_int_t
ngx_http_pckg_gone(ngx_http_request_t *r)
{
    time_t                          expires_time;
    ngx_int_t                       rc;
    ngx_http_pckg_core_loc_conf_t  *plcf;

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_pckg_core_module);

    expires_time = plcf->expires[NGX_HTTP_PCKG_EXPIRES_INDEX_GONE];
    if (expires_time >= 0) {
        rc = ngx_http_pckg_set_expires(r, expires_time);
        if (rc != NGX_OK) {
            ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                "ngx_http_pckg_gone: set expires failed");
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    return NGX_HTTP_GONE;
}


ngx_int_t
ngx_http_pckg_send_header(ngx_http_request_t *r, off_t content_length_n,
    ngx_str_t *content_type, time_t last_modified_time,
    ngx_uint_t expires_type)
{
    time_t                          expires_time;
    ngx_int_t                       rc;
    ngx_http_pckg_core_loc_conf_t  *plcf;

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_pckg_core_module);

    if (content_type != NULL) {
        r->headers_out.content_type = *content_type;
        r->headers_out.content_type_len = content_type->len;
    }

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = content_length_n;

    /* last modified */
    if (last_modified_time >= 0) {
        r->headers_out.last_modified_time = last_modified_time;

    } else if (plcf->last_modified_static >= 0) {
        r->headers_out.last_modified_time = plcf->last_modified_static;
    }

    /* expires */
    expires_time = plcf->expires[expires_type];
    if (expires_time >= 0) {
        rc = ngx_http_pckg_set_expires(r, expires_time);
        if (rc != NGX_OK) {
            ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                "ngx_http_pckg_send_header: set expires failed %i", rc);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    /* etag */
    rc = ngx_http_set_etag(r);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
            "ngx_http_pckg_send_header: set etag failed %i", rc);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* send the response headers */
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
            "ngx_http_pckg_send_header: sed header failed %i", rc);
        return rc;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_pckg_send_response(ngx_http_request_t *r, ngx_str_t *response)
{
    ngx_buf_t    *b;
    ngx_int_t     rc;
    ngx_chain_t   out;

    if (r->header_only || r->method == NGX_HTTP_HEAD) {
        return NGX_OK;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
            "ngx_http_pckg_send_response: alloc failed");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = response->data;
    b->last = response->data + response->len;
    b->temporary = (response->len > 0) ? 1 : 0;
    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    if (rc != NGX_OK && rc != NGX_AGAIN) {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
            "ngx_http_pckg_send_response: output filter failed %i", rc);
        return rc;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_pckg_status_to_ngx_error(ngx_http_request_t *r, vod_status_t rc)
{
    if (rc < VOD_ERROR_FIRST || rc >= VOD_ERROR_LAST) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return ngx_http_pckg_error_map[rc - VOD_ERROR_FIRST];
}


void
ngx_http_pckg_get_bitrate_estimator(ngx_http_request_t *r,
    ngx_http_pckg_container_t *container, media_info_t **media_infos,
    uint32_t count, media_bitrate_estimator_t *result)
{
    if (container->get_bitrate_estimator == NULL) {
        media_null_bitrate_estimator(*result);
        return;
    }

    container->get_bitrate_estimator(r, media_infos, count, result);
}


uint32_t
ngx_http_pckg_estimate_bitrate(ngx_http_request_t *r,
    ngx_http_pckg_container_t *container, media_info_t **media_infos,
    uint32_t count, uint32_t segment_duration, ngx_uint_t offset)
{
    uint32_t                    i;
    uint32_t                    result;
    uint32_t                   *bitrate;
    media_bitrate_estimator_t  *est;
    media_bitrate_estimator_t   estimators[KMP_MEDIA_COUNT];

    ngx_http_pckg_get_bitrate_estimator(r, container, media_infos, count,
        estimators);

    result = 0;
    for (i = 0; i < count; i++) {

        bitrate = (void *) ((u_char *) media_infos[i] + offset);

        est = &estimators[i];
        result += media_bitrate_estimate(*est, *bitrate, segment_duration);
    }

    return result;
}
