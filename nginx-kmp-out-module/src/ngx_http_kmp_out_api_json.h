/* auto-generated by generate_json_header.py */

#ifndef ngx_array_entries
#define ngx_array_entries(x)     (sizeof(x) / sizeof(x[0]))
#endif

#ifndef ngx_copy_fix
#define ngx_copy_fix(dst, src)   ngx_copy(dst, (src), sizeof(src) - 1)
#endif

#ifndef ngx_copy_str
#define ngx_copy_str(dst, src)   ngx_copy(dst, (src).data, (src).len)
#endif

/* ngx_http_kmp_out_api_upstream_json reader */

typedef struct {
    ngx_str_t  src_id;
} ngx_http_kmp_out_api_upstream_json_t;


static ngx_json_prop_t  ngx_http_kmp_out_api_upstream_json_src_id = {
    ngx_string("src_id"),
    3400677718ULL,
    NGX_JSON_STRING,
    ngx_json_set_str_slot,
    offsetof(ngx_http_kmp_out_api_upstream_json_t, src_id),
    NULL
};


static ngx_json_prop_t  *ngx_http_kmp_out_api_upstream_json[] = {
    &ngx_http_kmp_out_api_upstream_json_src_id,
};


/* ngx_http_kmp_out_api_json writer */

static size_t
ngx_http_kmp_out_api_json_get_size(void *obj)
{
    size_t  result;

    result =
        sizeof("{\"version\":\"") - 1 +
            ngx_json_str_get_size(&ngx_kmp_out_version) +
        sizeof("\",\"nginx_version\":\"") - 1 +
            ngx_json_str_get_size(&ngx_kmp_out_nginx_version) +
        sizeof("\",\"compiler\":\"") - 1 +
            ngx_json_str_get_size(&ngx_kmp_out_compiler) +
        sizeof("\",\"built\":\"") - 1 +
            ngx_json_str_get_size(&ngx_kmp_out_built) +
        sizeof("\",\"pid\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"uptime\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"tracks\":") - 1 + ngx_kmp_out_tracks_json_get_size(obj) +
        sizeof("}") - 1;

    return result;
}


static u_char *
ngx_http_kmp_out_api_json_write(u_char *p, void *obj)
{
    p = ngx_copy_fix(p, "{\"version\":\"");
    p = ngx_json_str_write(p, &ngx_kmp_out_version);
    p = ngx_copy_fix(p, "\",\"nginx_version\":\"");
    p = ngx_json_str_write(p, &ngx_kmp_out_nginx_version);
    p = ngx_copy_fix(p, "\",\"compiler\":\"");
    p = ngx_json_str_write(p, &ngx_kmp_out_compiler);
    p = ngx_copy_fix(p, "\",\"built\":\"");
    p = ngx_json_str_write(p, &ngx_kmp_out_built);
    p = ngx_copy_fix(p, "\",\"pid\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) ngx_getpid());
    p = ngx_copy_fix(p, ",\"uptime\":");
    p = ngx_sprintf(p, "%i", (ngx_int_t) (ngx_cached_time->sec -
        ngx_kmp_out_start_time));
    p = ngx_copy_fix(p, ",\"tracks\":");
    p = ngx_kmp_out_tracks_json_write(p, obj);
    *p++ = '}';

    return p;
}
