/* auto-generated by generate_json_builder.py */

#ifndef ngx_copy_fix
#define ngx_copy_fix(dst, src)   ngx_copy(dst, (src), sizeof(src) - 1)
#endif


size_t
ngx_rtmp_kmp_api_video_codec_info_json_get_size(ngx_rtmp_codec_ctx_t *obj)
{
    size_t  result =
        sizeof("{\"width\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"height\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"duration\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"frame_rate\":") - 1 + NGX_INT64_LEN + 3 +
        sizeof(",\"video_data_rate\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"video_codec_id\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"avc_profile\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"avc_compat\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"avc_level\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"avc_nal_bytes\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"avc_ref_frames\":") - 1 + NGX_INT_T_LEN +
        sizeof("}") - 1;

    return result;
}

u_char *
ngx_rtmp_kmp_api_video_codec_info_json_write(u_char *p, ngx_rtmp_codec_ctx_t
    *obj)
{
    p = ngx_copy_fix(p, "{\"width\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->width);
    p = ngx_copy_fix(p, ",\"height\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->height);
    p = ngx_copy_fix(p, ",\"duration\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->duration);
    p = ngx_copy_fix(p, ",\"frame_rate\":");
    p = ngx_sprintf(p, "%.2f", (double) obj->frame_rate);
    p = ngx_copy_fix(p, ",\"video_data_rate\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->video_data_rate);
    p = ngx_copy_fix(p, ",\"video_codec_id\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->video_codec_id);
    p = ngx_copy_fix(p, ",\"avc_profile\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->avc_profile);
    p = ngx_copy_fix(p, ",\"avc_compat\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->avc_compat);
    p = ngx_copy_fix(p, ",\"avc_level\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->avc_level);
    p = ngx_copy_fix(p, ",\"avc_nal_bytes\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->avc_nal_bytes);
    p = ngx_copy_fix(p, ",\"avc_ref_frames\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->avc_ref_frames);
    *p++ = '}';

    return p;
}

size_t
ngx_rtmp_kmp_api_audio_codec_info_json_get_size(ngx_rtmp_codec_ctx_t *obj)
{
    size_t  result =
        sizeof("{\"audio_data_rate\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"audio_codec_id\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"aac_profile\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"aac_chan_conf\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"aac_sbr\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"aac_ps\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"sample_rate\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"sample_size\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"audio_channels\":") - 1 + NGX_INT_T_LEN +
        sizeof("}") - 1;

    return result;
}

u_char *
ngx_rtmp_kmp_api_audio_codec_info_json_write(u_char *p, ngx_rtmp_codec_ctx_t
    *obj)
{
    p = ngx_copy_fix(p, "{\"audio_data_rate\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->audio_data_rate);
    p = ngx_copy_fix(p, ",\"audio_codec_id\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->audio_codec_id);
    p = ngx_copy_fix(p, ",\"aac_profile\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->aac_profile);
    p = ngx_copy_fix(p, ",\"aac_chan_conf\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->aac_chan_conf);
    p = ngx_copy_fix(p, ",\"aac_sbr\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->aac_sbr);
    p = ngx_copy_fix(p, ",\"aac_ps\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->aac_ps);
    p = ngx_copy_fix(p, ",\"sample_rate\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->sample_rate);
    p = ngx_copy_fix(p, ",\"sample_size\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->sample_size);
    p = ngx_copy_fix(p, ",\"audio_channels\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->audio_channels);
    *p++ = '}';

    return p;
}

static size_t
ngx_rtmp_kmp_track_video_json_get_size(ngx_rtmp_session_t *obj,
    ngx_rtmp_kmp_publish_t *pi, ngx_rtmp_codec_ctx_t *codec_ctx)
{
    size_t  result =
        sizeof("\"input_type\":\"rtmp\",\"app\":\"") - 1 + obj->app.len +
            ngx_escape_json(NULL, obj->app.data, obj->app.len) +
        sizeof("\",\"flashver\":\"") - 1 + obj->flashver.len +
            ngx_escape_json(NULL, obj->flashver.data, obj->flashver.len) +
        sizeof("\",\"swf_url\":\"") - 1 + obj->swf_url.len +
            ngx_escape_json(NULL, obj->swf_url.data, obj->swf_url.len) +
        sizeof("\",\"tc_url\":\"") - 1 + obj->tc_url.len +
            ngx_escape_json(NULL, obj->tc_url.data, obj->tc_url.len) +
        sizeof("\",\"page_url\":\"") - 1 + obj->page_url.len +
            ngx_escape_json(NULL, obj->page_url.data, obj->page_url.len) +
        sizeof("\",\"addr\":\"") - 1 + obj->connection->addr_text.len +
            ngx_escape_json(NULL, obj->connection->addr_text.data,
            obj->connection->addr_text.len) +
        sizeof("\",\"connection\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"name\":\"") - 1 + pi->name.len + ngx_escape_json(NULL,
            pi->name.data, pi->name.len) +
        sizeof("\",\"type\":\"") - 1 + pi->type.len + ngx_escape_json(NULL,
            pi->type.data, pi->type.len) +
        sizeof("\",\"args\":\"") - 1 + pi->args.len + ngx_escape_json(NULL,
            pi->args.data, pi->args.len) +
        sizeof("\",\"media_type\":\"video\",\"codec_info\":") - 1 +
            ngx_rtmp_kmp_api_video_codec_info_json_get_size(codec_ctx);

    return result;
}

static u_char *
ngx_rtmp_kmp_track_video_json_write(u_char *p, ngx_rtmp_session_t *obj,
    ngx_rtmp_kmp_publish_t *pi, ngx_rtmp_codec_ctx_t *codec_ctx)
{
    p = ngx_copy_fix(p, "\"input_type\":\"rtmp\",\"app\":\"");
    p = (u_char *) ngx_escape_json(p, obj->app.data, obj->app.len);
    p = ngx_copy_fix(p, "\",\"flashver\":\"");
    p = (u_char *) ngx_escape_json(p, obj->flashver.data, obj->flashver.len);
    p = ngx_copy_fix(p, "\",\"swf_url\":\"");
    p = (u_char *) ngx_escape_json(p, obj->swf_url.data, obj->swf_url.len);
    p = ngx_copy_fix(p, "\",\"tc_url\":\"");
    p = (u_char *) ngx_escape_json(p, obj->tc_url.data, obj->tc_url.len);
    p = ngx_copy_fix(p, "\",\"page_url\":\"");
    p = (u_char *) ngx_escape_json(p, obj->page_url.data, obj->page_url.len);
    p = ngx_copy_fix(p, "\",\"addr\":\"");
    p = (u_char *) ngx_escape_json(p, obj->connection->addr_text.data,
        obj->connection->addr_text.len);
    p = ngx_copy_fix(p, "\",\"connection\":");
    p = ngx_sprintf(p, "%uA", (ngx_atomic_uint_t) obj->connection->number);
    p = ngx_copy_fix(p, ",\"name\":\"");
    p = (u_char *) ngx_escape_json(p, pi->name.data, pi->name.len);
    p = ngx_copy_fix(p, "\",\"type\":\"");
    p = (u_char *) ngx_escape_json(p, pi->type.data, pi->type.len);
    p = ngx_copy_fix(p, "\",\"args\":\"");
    p = (u_char *) ngx_escape_json(p, pi->args.data, pi->args.len);
    p = ngx_copy_fix(p, "\",\"media_type\":\"video\",\"codec_info\":");
    p = ngx_rtmp_kmp_api_video_codec_info_json_write(p, codec_ctx);

    return p;
}

static size_t
ngx_rtmp_kmp_track_audio_json_get_size(ngx_rtmp_session_t *obj,
    ngx_rtmp_kmp_publish_t *pi, ngx_rtmp_codec_ctx_t *codec_ctx)
{
    size_t  result =
        sizeof("\"input_type\":\"rtmp\",\"app\":\"") - 1 + obj->app.len +
            ngx_escape_json(NULL, obj->app.data, obj->app.len) +
        sizeof("\",\"flashver\":\"") - 1 + obj->flashver.len +
            ngx_escape_json(NULL, obj->flashver.data, obj->flashver.len) +
        sizeof("\",\"swf_url\":\"") - 1 + obj->swf_url.len +
            ngx_escape_json(NULL, obj->swf_url.data, obj->swf_url.len) +
        sizeof("\",\"tc_url\":\"") - 1 + obj->tc_url.len +
            ngx_escape_json(NULL, obj->tc_url.data, obj->tc_url.len) +
        sizeof("\",\"page_url\":\"") - 1 + obj->page_url.len +
            ngx_escape_json(NULL, obj->page_url.data, obj->page_url.len) +
        sizeof("\",\"addr\":\"") - 1 + obj->connection->addr_text.len +
            ngx_escape_json(NULL, obj->connection->addr_text.data,
            obj->connection->addr_text.len) +
        sizeof("\",\"connection\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"name\":\"") - 1 + pi->name.len + ngx_escape_json(NULL,
            pi->name.data, pi->name.len) +
        sizeof("\",\"type\":\"") - 1 + pi->type.len + ngx_escape_json(NULL,
            pi->type.data, pi->type.len) +
        sizeof("\",\"args\":\"") - 1 + pi->args.len + ngx_escape_json(NULL,
            pi->args.data, pi->args.len) +
        sizeof("\",\"media_type\":\"audio\",\"codec_info\":") - 1 +
            ngx_rtmp_kmp_api_audio_codec_info_json_get_size(codec_ctx);

    return result;
}

static u_char *
ngx_rtmp_kmp_track_audio_json_write(u_char *p, ngx_rtmp_session_t *obj,
    ngx_rtmp_kmp_publish_t *pi, ngx_rtmp_codec_ctx_t *codec_ctx)
{
    p = ngx_copy_fix(p, "\"input_type\":\"rtmp\",\"app\":\"");
    p = (u_char *) ngx_escape_json(p, obj->app.data, obj->app.len);
    p = ngx_copy_fix(p, "\",\"flashver\":\"");
    p = (u_char *) ngx_escape_json(p, obj->flashver.data, obj->flashver.len);
    p = ngx_copy_fix(p, "\",\"swf_url\":\"");
    p = (u_char *) ngx_escape_json(p, obj->swf_url.data, obj->swf_url.len);
    p = ngx_copy_fix(p, "\",\"tc_url\":\"");
    p = (u_char *) ngx_escape_json(p, obj->tc_url.data, obj->tc_url.len);
    p = ngx_copy_fix(p, "\",\"page_url\":\"");
    p = (u_char *) ngx_escape_json(p, obj->page_url.data, obj->page_url.len);
    p = ngx_copy_fix(p, "\",\"addr\":\"");
    p = (u_char *) ngx_escape_json(p, obj->connection->addr_text.data,
        obj->connection->addr_text.len);
    p = ngx_copy_fix(p, "\",\"connection\":");
    p = ngx_sprintf(p, "%uA", (ngx_atomic_uint_t) obj->connection->number);
    p = ngx_copy_fix(p, ",\"name\":\"");
    p = (u_char *) ngx_escape_json(p, pi->name.data, pi->name.len);
    p = ngx_copy_fix(p, "\",\"type\":\"");
    p = (u_char *) ngx_escape_json(p, pi->type.data, pi->type.len);
    p = ngx_copy_fix(p, "\",\"args\":\"");
    p = (u_char *) ngx_escape_json(p, pi->args.data, pi->args.len);
    p = ngx_copy_fix(p, "\",\"media_type\":\"audio\",\"codec_info\":");
    p = ngx_rtmp_kmp_api_audio_codec_info_json_write(p, codec_ctx);

    return p;
}
