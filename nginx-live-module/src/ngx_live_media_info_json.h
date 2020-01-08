/* auto-generated by generate_json_builder.py */

#ifndef ngx_copy_fix
#define ngx_copy_fix(dst, src)   ngx_copy(dst, (src), sizeof(src) - 1)
#endif


static size_t
ngx_live_media_info_json_video_get_size(kmp_media_info_t *obj, media_info_t
    *media_info)
{
    size_t  result =
        sizeof("{\"codec_id\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"bitrate\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"format\":\"") - 1 + sizeof(uint32_t) +
            ngx_escape_json(NULL, (u_char *) &media_info->format,
            sizeof(uint32_t)) +
        sizeof("\",\"codec_name\":\"") - 1 + media_info->codec_name.len +
            ngx_escape_json(NULL, media_info->codec_name.data,
            media_info->codec_name.len) +
        sizeof("\",\"extra_data\":\"") - 1 + media_info->extra_data.len * 2 +
        sizeof("\",\"width\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"height\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"frame_rate\":") - 1 + NGX_INT32_LEN + 3 +
        sizeof("}") - 1;

    return result;
}

static u_char *
ngx_live_media_info_json_video_write(u_char *p, kmp_media_info_t *obj,
    media_info_t *media_info)
{
    uint32_t  n, d;
    p = ngx_copy_fix(p, "{\"codec_id\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->codec_id);
    p = ngx_copy_fix(p, ",\"bitrate\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->bitrate);
    p = ngx_copy_fix(p, ",\"format\":\"");
    p = (u_char *) ngx_escape_json(p, (u_char *) &media_info->format,
        sizeof(uint32_t));
    p = ngx_copy_fix(p, "\",\"codec_name\":\"");
    p = (u_char *) ngx_escape_json(p, media_info->codec_name.data,
        media_info->codec_name.len);
    p = ngx_copy_fix(p, "\",\"extra_data\":\"");
    p = ngx_hex_dump(p, media_info->extra_data.data,
        media_info->extra_data.len);
    p = ngx_copy_fix(p, "\",\"width\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->u.video.width);
    p = ngx_copy_fix(p, ",\"height\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->u.video.height);
    p = ngx_copy_fix(p, ",\"frame_rate\":");
    n = obj->u.video.frame_rate.num;
    d = obj->u.video.frame_rate.denom;
    p = ngx_sprintf(p, "%uD.%02uD", (uint32_t) (n / d), (uint32_t) (n % d *
        100) / d);
    *p++ = '}';

    return p;
}

static size_t
ngx_live_media_info_json_audio_get_size(kmp_media_info_t *obj, media_info_t
    *media_info)
{
    size_t  result =
        sizeof("{\"codec_id\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"bitrate\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"format\":\"") - 1 + sizeof(uint32_t) +
            ngx_escape_json(NULL, (u_char *) &media_info->format,
            sizeof(uint32_t)) +
        sizeof("\",\"codec_name\":\"") - 1 + media_info->codec_name.len +
            ngx_escape_json(NULL, media_info->codec_name.data,
            media_info->codec_name.len) +
        sizeof("\",\"extra_data\":\"") - 1 + media_info->extra_data.len * 2 +
        sizeof("\",\"channels\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"bits_per_sample\":") - 1 + NGX_INT32_LEN +
        sizeof(",\"sample_rate\":") - 1 + NGX_INT32_LEN +
        sizeof("}") - 1;

    return result;
}

static u_char *
ngx_live_media_info_json_audio_write(u_char *p, kmp_media_info_t *obj,
    media_info_t *media_info)
{
    p = ngx_copy_fix(p, "{\"codec_id\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->codec_id);
    p = ngx_copy_fix(p, ",\"bitrate\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->bitrate);
    p = ngx_copy_fix(p, ",\"format\":\"");
    p = (u_char *) ngx_escape_json(p, (u_char *) &media_info->format,
        sizeof(uint32_t));
    p = ngx_copy_fix(p, "\",\"codec_name\":\"");
    p = (u_char *) ngx_escape_json(p, media_info->codec_name.data,
        media_info->codec_name.len);
    p = ngx_copy_fix(p, "\",\"extra_data\":\"");
    p = ngx_hex_dump(p, media_info->extra_data.data,
        media_info->extra_data.len);
    p = ngx_copy_fix(p, "\",\"channels\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->u.audio.channels);
    p = ngx_copy_fix(p, ",\"bits_per_sample\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->u.audio.bits_per_sample);
    p = ngx_copy_fix(p, ",\"sample_rate\":");
    p = ngx_sprintf(p, "%uD", (uint32_t) obj->u.audio.sample_rate);
    *p++ = '}';

    return p;
}
