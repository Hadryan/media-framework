#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_pckg_ksmp.h"
#include "ngx_pckg_ksmp_sgts.h"

#include "media/mp4/mp4_defs.h"
#include "media/avc_hevc_parser.h"
#include "media/avc_parser.h"
#include "media/hevc_parser.h"


#ifndef ngx_copy_fix
#define ngx_copy_fix(dst, src)   ngx_copy(dst, (src), sizeof(src) - 1)
#endif

#ifndef ngx_copy_str
#define ngx_copy_str(dst, src)   ngx_copy(dst, (src).data, (src).len)
#endif


static ngx_int_t
ngx_pckg_ksmp_read_channel(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                   rc;
    ngx_pckg_channel_t         *channel = obj;
    ngx_ksmp_channel_header_t  *h;

    if (channel->header.timescale != 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_str_get(rs, &channel->id) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: read id failed");
        return NGX_BAD_DATA;
    }

    h = &channel->header;
    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: read header failed");
        return NGX_BAD_DATA;
    }

    if (h->timescale < 1000) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: invalid timescale %uD",
            h->timescale);
        return NGX_BAD_DATA;
    }

    if (h->track_count <= 0 ||
        h->track_count > NGX_KSMP_MAX_TRACKS)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: invalid track count %uD",
            h->track_count);
        return NGX_BAD_DATA;
    }

    if (h->variant_count <= 0 ||
        h->variant_count > NGX_KSMP_MAX_VARIANTS)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: invalid variant count %uD",
            h->variant_count);
        return NGX_BAD_DATA;
    }

    if (ngx_array_init(&channel->tracks, channel->pool,
        h->track_count, sizeof(ngx_pckg_track_t)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: array init failed (1)");
        return NGX_ERROR;
    }

    if (ngx_array_init(&channel->variants, channel->pool,
        h->variant_count, sizeof(ngx_pckg_variant_t)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: array init failed (2)");
        return NGX_ERROR;
    }


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_CHANNEL, rs, channel);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: read blocks failed %i", rc);
        return rc;
    }

    if (channel->err_code != NGX_KSMP_ERR_SUCCESS) {
        return NGX_OK;
    }


    if (channel->tracks.nelts != h->track_count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: track count mismatch, "
            "expected: %uD, actual: %ui",
            h->track_count, channel->tracks.nelts);
        return NGX_BAD_DATA;
    }

    if (channel->variants.nelts != h->variant_count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: variant count mismatch, "
            "expected: %uD, actual: %ui",
            h->variant_count, channel->variants.nelts);
        return NGX_BAD_DATA;
    }

    if (channel->timeline.header.target_duration == 0 &&
        (channel->flags & NGX_KSMP_FLAG_TIMELINE))
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: missing timeline block");
        return NGX_BAD_DATA;
    }

    if (channel->segment_index == NULL &&
        (channel->flags & NGX_KSMP_FLAG_MEDIA))
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_channel: missing segment index block");
        return NGX_BAD_DATA;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_timeline(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    uint32_t                     period_count;
    ngx_int_t                    rc;
    ngx_pckg_channel_t          *channel = obj;
    ngx_pckg_timeline_t         *timeline;
    ngx_ksmp_timeline_header_t  *h;

    timeline = &channel->timeline;

    if (timeline->header.target_duration != 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_str_get(rs, &timeline->id) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: read id failed");
        return NGX_BAD_DATA;
    }

    h = &timeline->header;
    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: read header failed");
        return NGX_BAD_DATA;
    }

    if (channel->flags & NGX_KSMP_FLAG_PERIODS) {
        period_count = h->period_count;

        if (period_count <= 0 || period_count > NGX_KSMP_MAX_PERIODS) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_timeline: invalid period count %uD",
                period_count);
            return NGX_BAD_DATA;
        }

    } else {
        period_count = 0;
    }

    if (h->target_duration <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: invalid target duration %uD",
            h->target_duration);
        return NGX_BAD_DATA;
    }

    if (ngx_array_init(&timeline->periods, channel->pool,
        period_count, sizeof(ngx_pckg_period_t)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: array init failed");
        return NGX_ERROR;
    }

    timeline->channel = channel;


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_TIMELINE, rs, timeline);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: read blocks failed %i", rc);
        return rc;
    }


    if (timeline->periods.nelts != period_count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_timeline: period count mismatch, "
            "expected: %uD, actual: %ui",
            period_count, timeline->periods.nelts);
        return NGX_BAD_DATA;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_period(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    uint64_t                    count;
    uint64_t                    duration;
    ngx_str_t                   segments;
    ngx_uint_t                  i, n;
    ngx_pckg_period_t          *period;
    ngx_pckg_timeline_t        *timeline = obj;
    ngx_ksmp_period_header_t   *h;
    ngx_ksmp_segment_repeat_t  *elt;

    if (timeline->pending_segment) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: got period after pending segment");
        return NGX_BAD_DATA;
    }

    period = ngx_array_push(&timeline->periods);
    if (period == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_period: push failed");
        return NGX_ERROR;
    }

    h = &period->header;
    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: read header failed");
        return NGX_BAD_DATA;
    }

    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    if (h->time < timeline->last_time) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: period time %L before last time %L",
            h->time, timeline->last_time);
        return NGX_BAD_DATA;
    }

    if (h->segment_index < timeline->last_segment) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: "
            "period index %uD before last index %uD",
            h->segment_index, timeline->last_segment);
        return NGX_BAD_DATA;
    }

    period->timeline = timeline;

    ngx_mem_rstream_get_left(rs, &segments);

    period->nelts = segments.len / sizeof(period->elts[0]);
    if (period->nelts <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: no segments");
        return NGX_BAD_DATA;
    }

    period->elts = ngx_palloc(timeline->channel->pool, segments.len);
    if (period->elts == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_period: alloc failed");
        return NGX_ERROR;
    }

    ngx_memcpy(period->elts, segments.data, segments.len);


    count = 0;
    duration = 0;

    n = period->nelts - 1;
    for (i = 0; i < n; i++) {

        elt = &period->elts[i];
        if (elt->count <= 0 || elt->duration <= 0) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_period: zero repeat/duration");
            return NGX_BAD_DATA;
        }

        count += elt->count;
        duration += (uint64_t) elt->count * elt->duration;
    }

    elt = &period->elts[n];
    if (elt->duration == NGX_KSMP_PENDING_SEGMENT_DURATION) {
        if (elt->count != 1) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_period: "
                "invalid pending segment repeat %uD", elt->count);
            return NGX_BAD_DATA;
        }

        timeline->pending_segment = 1;

    } else if (elt->count <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: zero repeat");
        return NGX_BAD_DATA;
    }

    count += elt->count;
    duration += (uint64_t) elt->count * elt->duration;

    if (count > NGX_KSMP_INVALID_SEGMENT_INDEX - h->segment_index) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: segment index overflow, "
            "index: %uD, count: %uL", h->segment_index, count);
        return NGX_BAD_DATA;
    }

    if (duration > (uint64_t) (LLONG_MAX - h->time)) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_period: segment time overflow, "
            "time: %L, duration: %uL", h->time, duration);
        return NGX_BAD_DATA;
    }

    period->segment_count = count;
    period->duration = duration;

    timeline->duration += duration;
    timeline->last_time = h->time + duration;
    timeline->last_segment = h->segment_index + count;
    timeline->segment_count += period->segment_count;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_track(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                 rc;
    ngx_pckg_track_t         *track;
    ngx_pckg_variant_t       *variant = obj;
    ngx_pckg_channel_t       *channel;
    ngx_ksmp_track_header_t  *h;

    channel = variant->channel;

    track = ngx_array_push(&channel->tracks);
    if (track == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_track: push failed");
        return NGX_ERROR;
    }

    ngx_memzero(track, sizeof(*track));

    h = &track->header;

    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track: read header failed");
        return NGX_BAD_DATA;
    }

    if (h->media_type >= KMP_MEDIA_COUNT) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track: invalid media type %uD",
            h->media_type);
        return NGX_BAD_DATA;
    }

    if (variant->tracks[h->media_type] != NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track: variant already has media type %uD",
            h->media_type);
        return NGX_BAD_DATA;
    }


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    track->channel = channel;

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_TRACK, rs, track);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_track: read blocks failed %i", rc);
        return rc;
    }


    if (channel->flags & NGX_KSMP_FLAG_MEDIA_INFO) {
        if (track->media_info.nelts <= 0) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_track: missing media info block");
            return NGX_BAD_DATA;
        }

        ngx_pckg_media_info_iter_reset(&track->media_info_iter, track);
    }

    if (track->segment_info.nelts <= 0 &&
        (channel->flags & NGX_KSMP_FLAG_SEGMENT_INFO))
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track: missing segment info block");
        return NGX_BAD_DATA;
    }

    variant->tracks[h->media_type] = track;
    variant->track_count++;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_segment_info(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_str_t          info;
    ngx_pckg_track_t  *track = obj;

    if (track->segment_info.elts != NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_info: duplicate block");
        return NGX_BAD_DATA;
    }

    ngx_mem_rstream_get_left(rs, &info);

    track->segment_info.nelts = info.len / sizeof(track->segment_info.elts[0]);
    if (track->segment_info.nelts <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_info: no segments");
        return NGX_BAD_DATA;
    }

    track->segment_info.elts = ngx_palloc(track->channel->pool, info.len);
    if (track->segment_info.elts == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_info: alloc failed");
        return NGX_ERROR;
    }

    ngx_memcpy(track->segment_info.elts, info.data, info.len);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_media_info_queue(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                            rc;
    ngx_pckg_track_t                    *track = obj;
    ngx_pckg_channel_t                  *channel;
    ngx_ksmp_media_info_queue_header_t   h;

    if (track->media_info.elts) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info_queue: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_read(rs, &h, sizeof(h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info_queue: read header failed");
        return NGX_BAD_DATA;
    }

    if (h.count <= 0 || h.count > NGX_KSMP_MAX_MEDIA_INFOS) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info_queue: invalid count %uD",
            h.count);
        return NGX_BAD_DATA;
    }

    channel = track->channel;
    if (ngx_array_init(&track->media_info, channel->pool, h.count,
        sizeof(ngx_pckg_media_info_t)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info_queue: array init failed");
        return NGX_ERROR;
    }


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_MEDIA_INFO_QUEUE, rs, track);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info_queue: read blocks failed %i", rc);
        return rc;
    }


    if (track->media_info.nelts != h.count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info_queue: "
            "media info count mismatch, expected: %uD, actual: %ui",
            h.count, track->media_info.nelts);
        return NGX_BAD_DATA;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_track_parts(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                       rc;
    ngx_pckg_track_t               *track = obj;
    ngx_pckg_channel_t             *channel;
    ngx_ksmp_track_parts_header_t   h;

    if (track->parts.elts) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track_parts: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_read(rs, &h, sizeof(h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track_parts: read header failed");
        return NGX_BAD_DATA;
    }

    if (h.count <= 0 || h.count > NGX_KSMP_MAX_SEGMENT_PARTS) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track_parts: invalid count %uD", h.count);
        return NGX_BAD_DATA;
    }

    channel = track->channel;
    if (ngx_array_init(&track->parts, channel->pool, h.count,
        sizeof(ngx_pckg_segment_parts_t)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_track_parts: array init failed");
        return NGX_ERROR;
    }


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_TRACK_PARTS, rs, track);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_track_parts: read blocks failed %i", rc);
        return rc;
    }


    if (track->parts.nelts != h.count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_track_parts: "
            "segment part count mismatch, expected: %uD, actual: %ui",
            h.count, track->parts.nelts);
        return NGX_BAD_DATA;
    }

    track->parts_cur = track->parts.elts;
    track->parts_end = track->parts_cur + track->parts.nelts;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_segment_parts(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    uint32_t                          d;
    uint32_t                          mask;
    ngx_str_t                         data;
    ngx_uint_t                        i, n;
    ngx_pckg_track_t                 *track = obj;
    ngx_pckg_segment_parts_t         *parts, *last;
    ngx_ksmp_segment_parts_header_t   h;

    if (ngx_mem_rstream_read(rs, &h, sizeof(h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_parts: read header failed");
        return NGX_BAD_DATA;
    }

    if (h.segment_index == NGX_KSMP_INVALID_SEGMENT_INDEX) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_parts: invalid segment index");
        return NGX_BAD_DATA;
    }

    if (track->parts.nelts > 0) {
        parts = track->parts.elts;
        last = &parts[track->parts.nelts - 1];

        if (h.segment_index <= last->segment_index) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_segment_parts: "
                "segment index %uD smaller than last %uD",
                h.segment_index, last->segment_index);
            return NGX_BAD_DATA;
        }

        if (last->duration[last->count - 1] == NGX_KSMP_PART_PRELOAD_HINT) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_segment_parts: "
                "got parts after preload hint");
            return NGX_BAD_DATA;
        }
    }

    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    parts = ngx_array_push(&track->parts);
    if (parts == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_parts: push failed");
        return NGX_ERROR;
    }

    ngx_mem_rstream_get_left(rs, &data);

    n = data.len / sizeof(parts->duration[0]);
    if (n <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_parts: no parts");
        return NGX_BAD_DATA;
    }

    parts->duration = ngx_palloc(track->channel->pool, data.len);
    if (parts->duration == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_parts: alloc failed");
        return NGX_ERROR;
    }

    ngx_memcpy(parts->duration, data.data, data.len);

    parts->segment_index = h.segment_index;
    parts->count = n;

    if (track->header.media_type == KMP_MEDIA_SUBTITLE) {
        mask = ~NGX_KSMP_PART_GAP;

    } else {
        mask = NGX_MAX_UINT32_VALUE;
    }

    n--;
    for (i = 0; i < n; i++) {

        d = parts->duration[i];
        if (vod_all_flags_set(d, NGX_KSMP_PART_GAP
            | NGX_KSMP_PART_INDEPENDENT))
        {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_segment_parts: "
                "invalid part flags 0x%uxD", d);
            return NGX_BAD_DATA;
        }

        parts->duration[i] &= mask;
    }

    d = parts->duration[n];
    if (d != NGX_KSMP_PART_PRELOAD_HINT) {
        if (vod_all_flags_set(d, NGX_KSMP_PART_GAP
            | NGX_KSMP_PART_INDEPENDENT))
        {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_segment_parts: "
                "invalid last part flags 0x%uxD", d);
            return NGX_BAD_DATA;
        }

        parts->duration[n] &= mask;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_rendition_reports(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                             rc;
    ngx_pckg_channel_t                   *channel = obj;
    ngx_ksmp_rendition_reports_header_t   h;

    if (channel->rrs.elts) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_reports: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_read(rs, &h, sizeof(h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_reports: read header failed");
        return NGX_BAD_DATA;
    }

    if (h.count <= 0 || h.count > NGX_KSMP_MAX_VARIANTS) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_reports: invalid count %uD",
            h.count);
        return NGX_BAD_DATA;
    }

    if (ngx_array_init(&channel->rrs, channel->pool, h.count,
        sizeof(ngx_pckg_rendition_report_t)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_reports: array init failed");
        return NGX_ERROR;
    }


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_RENDITION_REPORTS, rs, channel);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_reports: read blocks failed %i", rc);
        return rc;
    }


    if (channel->rrs.nelts != h.count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_reports: "
            "rendition report count mismatch, expected: %uD, actual: %ui",
            h.count, channel->rrs.nelts);
        return NGX_BAD_DATA;
    }

    channel->rr_last_sequence = h.last_sequence;
    channel->rr_last_part_index = h.last_part_index;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_rendition_report(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    uint32_t                      media_type;
    ngx_str_t                     id;
    ngx_str_t                     data;
    ngx_uint_t                    i, n;
    ngx_pckg_channel_t           *channel = obj;
    ngx_pckg_rendition_report_t  *rr;

    if (ngx_mem_rstream_str_get(rs, &id) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_report: read id failed");
        return NGX_BAD_DATA;
    }

    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    rr = ngx_array_push(&channel->rrs);
    if (rr == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_report: push failed");
        return NGX_ERROR;
    }

    ngx_mem_rstream_get_left(rs, &data);

    n = data.len / sizeof(rr->elts[0]);
    if (n <= 0 || n > KMP_MEDIA_COUNT) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_report: invalid count %ui", n);
        return NGX_BAD_DATA;
    }

    rr->elts = ngx_palloc(channel->pool, data.len);
    if (rr->elts == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_rendition_report: alloc failed");
        return NGX_ERROR;
    }

    ngx_memcpy(rr->elts, data.data, data.len);

    rr->variant_id = id;
    rr->nelts = n;

    for (i = 0; i < n; i++) {
        media_type = rr->elts[i].media_type;
        if (media_type >= KMP_MEDIA_COUNT) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_rendition_report: "
                "invalid media type %uD", media_type);
            return NGX_BAD_DATA;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_parse_avc_transfer_char(ngx_pckg_channel_t *channel,
    media_info_t *mi)
{
    void               *parser_ctx;
    vod_status_t        rc;
    request_context_t   request_context;

    ngx_memzero(&request_context, sizeof(request_context));
    request_context.pool = channel->pool;
    request_context.log = channel->log;

    rc = avc_hevc_parser_init_ctx(&request_context, &parser_ctx);
    if (rc != VOD_OK) {
        return NGX_ERROR;
    }

    rc = avc_parser_parse_extra_data(parser_ctx, &mi->extra_data,
        NULL, NULL);
    if (rc != VOD_OK) {
        return rc == VOD_BAD_DATA ? NGX_BAD_DATA : NGX_ERROR;
    }

    mi->u.video.transfer_characteristics =
        avc_parser_get_transfer_characteristics(parser_ctx);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_parse_avc_extra_data(ngx_pckg_channel_t *channel,
    media_info_t *mi)
{
    u_char  *p;
    size_t   size;

    size = codec_config_avcc_nal_units_get_size(channel->log, &mi->extra_data);
    if (size <= 0) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse_avc_extra_data: parse failed");
        return NGX_BAD_DATA;
    }

    p = ngx_pnalloc(channel->pool, size);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse_avc_extra_data: alloc failed");
        return NGX_ERROR;
    }

    mi->parsed_extra_data.data = p;
    p = codec_config_avcc_nal_units_write(p, &mi->extra_data);
    mi->parsed_extra_data.len = p - mi->parsed_extra_data.data;

    if (mi->parsed_extra_data.len != size) {
        ngx_log_error(NGX_LOG_ALERT, channel->log, 0,
            "ngx_pckg_ksmp_parse_avc_extra_data: "
            "actual extra data size %uz different from calculated %uz",
            mi->parsed_extra_data.len, size);
        return NGX_ERROR;
    }

    vod_log_buffer(VOD_LOG_DEBUG_LEVEL, channel->log, 0,
        "ngx_pckg_ksmp_parse_avc_extra_data: parsed extra data ",
        mi->parsed_extra_data.data, mi->parsed_extra_data.len);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_parse_hevc_transfer_char(ngx_pckg_channel_t *channel,
    media_info_t *mi)
{
    void               *parser_ctx;
    vod_status_t        rc;
    request_context_t   request_context;

    ngx_memzero(&request_context, sizeof(request_context));
    request_context.pool = channel->pool;
    request_context.log = channel->log;

    rc = avc_hevc_parser_init_ctx(&request_context, &parser_ctx);
    if (rc != VOD_OK) {
        return NGX_ERROR;
    }

    rc = hevc_parser_parse_extra_data(parser_ctx, &mi->extra_data,
        NULL, NULL);
    if (rc != VOD_OK) {
        return rc == VOD_BAD_DATA ? NGX_BAD_DATA : NGX_ERROR;
    }

    mi->u.video.transfer_characteristics =
        hevc_parser_get_transfer_characteristics(parser_ctx);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_parse_hevc_extra_data(ngx_pckg_channel_t *channel,
    media_info_t *mi)
{
    u_char  *p;
    size_t   size;

    size = codec_config_hvcc_nal_units_get_size(channel->log, &mi->extra_data);
    if (size <= 0) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse_hevc_extra_data: parse failed");
        return NGX_BAD_DATA;
    }

    p = ngx_pnalloc(channel->pool, size);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse_hevc_extra_data: alloc failed");
        return NGX_ERROR;
    }

    mi->parsed_extra_data.data = p;
    p = codec_config_hvcc_nal_units_write(p, &mi->extra_data);
    mi->parsed_extra_data.len = p - mi->parsed_extra_data.data;

    if (mi->parsed_extra_data.len != size) {
        ngx_log_error(NGX_LOG_ALERT, channel->log, 0,
            "ngx_pckg_ksmp_parse_hevc_extra_data: "
            "actual extra data size %uz different from calculated %uz",
            mi->parsed_extra_data.len, size);
        return NGX_ERROR;
    }

    vod_log_buffer(VOD_LOG_DEBUG_LEVEL, channel->log, 0,
        "ngx_pckg_ksmp_parse_hevc_extra_data: parsed extra data ",
        mi->parsed_extra_data.data, mi->parsed_extra_data.len);

    return NGX_OK;
}


ngx_int_t
ngx_pckg_ksmp_parse_media_info(ngx_pckg_channel_t *channel,
    ngx_pckg_media_info_t *node)
{
    vod_status_t       rc;
    media_info_t      *dest = &node->media_info;
    kmp_media_info_t  *src = &node->kmp_media_info;

    dest->extra_data = node->extra_data;
    dest->parsed_extra_data.data = NULL;

    switch (src->media_type) {

    case KMP_MEDIA_VIDEO:

        switch (src->codec_id) {

        case KMP_CODEC_VIDEO_H264:
            dest->u.video.nal_packet_size_length =
                codec_config_avcc_get_nal_length_size(channel->log,
                    &dest->extra_data);
            if (dest->u.video.nal_packet_size_length == 0) {
                return NGX_BAD_DATA;
            }

            if (channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_TRANSFER_CHAR) {
                /* ignore errors - if we fail, just assume no transfer char */
                (void) ngx_pckg_ksmp_parse_avc_transfer_char(channel, dest);
            }

            if (channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_EXTRA_DATA) {
                rc = ngx_pckg_ksmp_parse_avc_extra_data(channel, dest);
                if (rc != NGX_OK) {
                    return rc;
                }
            }

            dest->codec_id = VOD_CODEC_ID_AVC;
            dest->format = FORMAT_AVC1;
            break;

        case KMP_CODEC_VIDEO_H265:
            dest->u.video.nal_packet_size_length =
                codec_config_hvcc_get_nal_length_size(channel->log,
                    &dest->extra_data);
            if (dest->u.video.nal_packet_size_length == 0) {
                return NGX_BAD_DATA;
            }

            if (channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_TRANSFER_CHAR) {
                /* ignore errors - if we fail, just assume no transfer char */
                (void) ngx_pckg_ksmp_parse_hevc_transfer_char(channel, dest);
            }

            if (channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_EXTRA_DATA) {
                rc = ngx_pckg_ksmp_parse_hevc_extra_data(channel, dest);
                if (rc != NGX_OK) {
                    return rc;
                }
            }

            dest->codec_id = VOD_CODEC_ID_HEVC;
            dest->format = FORMAT_HVC1;
            break;

        default:
            ngx_log_error(NGX_LOG_ERR, channel->log, 0,
                "ngx_pckg_ksmp_parse_media_info: invalid video codec id %uD",
                src->codec_id);
            return NGX_BAD_DATA;
        }

        if (src->u.video.frame_rate.num <= 0) {
            src->u.video.frame_rate.num = node->header.stats.frame_rate_max;
            src->u.video.frame_rate.denom = 100;

        } else if (src->u.video.frame_rate.denom <= 0) {
            ngx_log_error(NGX_LOG_ERR, channel->log, 0,
                "ngx_pckg_ksmp_parse_media_info: invalid video frame rate");
            return NGX_BAD_DATA;
        }

        dest->media_type = MEDIA_TYPE_VIDEO;

        dest->u.video.width = src->u.video.width;
        dest->u.video.height = src->u.video.height;
        dest->u.video.frame_rate_num = src->u.video.frame_rate.num;
        dest->u.video.frame_rate_denom = src->u.video.frame_rate.denom;
        dest->u.video.cea_captions = src->u.video.cea_captions;

        if ((channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_CODEC_NAME) &&
            codec_config_get_video_codec_name(channel->log, dest) != VOD_OK)
        {
            ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
                "ngx_pckg_ksmp_parse_media_info: "
                "failed to get video codec name");
            return NGX_BAD_DATA;
        }

        break;

    case KMP_MEDIA_AUDIO:

        switch (src->codec_id) {

        case KMP_CODEC_AUDIO_AAC:

            if (channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_EXTRA_DATA) {
                rc = codec_config_mp4a_config_parse(channel->log,
                    &dest->extra_data, &dest->u.audio.codec_config);
                if (rc != VOD_OK) {
                    ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
                        "ngx_pckg_ksmp_parse_media_info: "
                        "failed to parse mp4a config");
                    return NGX_BAD_DATA;
                }
            }

            dest->codec_id = VOD_CODEC_ID_AAC;
            dest->format = FORMAT_MP4A;
            dest->u.audio.object_type_id = 0x40;
            break;

        case KMP_CODEC_AUDIO_MP3:
            dest->codec_id = VOD_CODEC_ID_MP3;
            dest->format = FORMAT_MP4A;
            dest->u.audio.object_type_id = src->u.audio.sample_rate > 24000 ?
                0x6b : 0x69;
            break;

        case KMP_CODEC_AUDIO_AC3:
            dest->codec_id = VOD_CODEC_ID_AC3;
            dest->format = FORMAT_AC3;
            break;

        case KMP_CODEC_AUDIO_EC3:
            dest->codec_id = VOD_CODEC_ID_EAC3;
            dest->format = FORMAT_EAC3;
            break;

        case KMP_CODEC_AUDIO_OPUS:
            dest->codec_id = VOD_CODEC_ID_OPUS;
            dest->format = FORMAT_OPUS;
            break;

        default:
            ngx_log_error(NGX_LOG_ERR, channel->log, 0,
                "ngx_pckg_ksmp_parse_media_info: invalid audio codec id %uD",
                src->codec_id);
            return NGX_BAD_DATA;
        }

        dest->media_type = MEDIA_TYPE_AUDIO;
        dest->u.audio.channels = src->u.audio.channels;
        dest->u.audio.channel_layout = src->u.audio.channel_layout;
        dest->u.audio.bits_per_sample = src->u.audio.bits_per_sample;
        dest->u.audio.sample_rate = src->u.audio.sample_rate;

        if ((channel->parse_flags & NGX_PCKG_KSMP_PARSE_FLAG_CODEC_NAME) &&
            codec_config_get_audio_codec_name(channel->log, dest) != VOD_OK)
        {
            ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
                "ngx_pckg_ksmp_parse_media_info: "
                "failed to get audio codec name");
            return NGX_BAD_DATA;
        }

        break;

    case KMP_MEDIA_SUBTITLE:
        if (src->codec_id != KMP_CODEC_SUBTITLE_WEBVTT) {
            ngx_log_error(NGX_LOG_ERR, channel->log, 0,
                "ngx_pckg_ksmp_parse_media_info: "
                "invalid subtitle codec id %uD", src->codec_id);
            return NGX_BAD_DATA;
        }

        dest->media_type = MEDIA_TYPE_SUBTITLE;
        dest->codec_id = VOD_CODEC_ID_WEBVTT;
        break;

    default:
        ngx_log_error(NGX_LOG_ALERT, channel->log, 0,
            "ngx_pckg_ksmp_parse_media_info: invalid media type %uD",
            src->media_type);
        return NGX_BAD_DATA;
    }

    if (src->bitrate < node->header.stats.bitrate_max) {
        src->bitrate = node->header.stats.bitrate_max;
    }

    dest->bitrate = src->bitrate;
    dest->timescale = src->timescale;

    if (node->header.stats.bitrate_count > 1) {
        dest->avg_bitrate = node->header.stats.bitrate_sum
            / node->header.stats.bitrate_count;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_media_info(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                      rc;
    ngx_pckg_track_t              *track = obj;
    ngx_pckg_media_info_t         *media_info;
    ngx_ksmp_media_info_header_t  *h;

    media_info = ngx_array_push(&track->media_info);
    if (media_info == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info: push failed");
        return NGX_ERROR;
    }

    h = &media_info->header;

    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info: read header failed");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_read(rs, &media_info->kmp_media_info,
        sizeof(media_info->kmp_media_info)) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info: read kmp media info failed");
        return NGX_BAD_DATA;
    }

    if (track->last_media_info != NULL &&
        h->segment_index <= track->last_media_info->header.segment_index)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info: "
            "segment index %uD less than previous segment index %uD",
            h->segment_index,
            track->last_media_info->header.segment_index);
        return NGX_BAD_DATA;
    }

    if (media_info->kmp_media_info.media_type != track->header.media_type) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_media_info: "
            "media info type %uD doesn't match track %uD",
            media_info->kmp_media_info.media_type, track->header.media_type);
        return NGX_BAD_DATA;
    }

    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    ngx_mem_rstream_get_left(rs, &media_info->extra_data);

    ngx_memzero(&media_info->media_info, sizeof(media_info->media_info));
    media_info->media_info.codec_name.data = media_info->codec_name;

    rc = ngx_pckg_ksmp_parse_media_info(track->channel, media_info);
    if (rc != NGX_OK) {
        return rc;
    }

    track->last_media_info = media_info;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_variant(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_str_t            id;
    ngx_int_t            rc;
    ngx_ksmp_variant_t  *h;
    ngx_pckg_variant_t  *variant;
    ngx_pckg_channel_t  *channel = obj;

    if (ngx_mem_rstream_str_get(rs, &id) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: read id failed");
        return NGX_BAD_DATA;
    }

    variant = ngx_array_push(&channel->variants);
    if (variant == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: push failed");
        return NGX_ERROR;
    }

    h = &variant->header;
    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: read header failed");
        return NGX_BAD_DATA;
    }

    if (h->role > ngx_ksmp_variant_role_count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: invalid role %uD",
            h->role);
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_str_get(rs, &variant->label) != NGX_OK ||
        ngx_mem_rstream_str_get(rs, &variant->lang) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: read label/lang failed");
        return NGX_BAD_DATA;
    }

    if (h->role == ngx_ksmp_variant_role_main) {
        variant->label.len = 0;
    }

    ngx_memzero(variant->tracks, sizeof(variant->tracks));
    variant->track_count = 0;

    variant->channel = channel;

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_VARIANT, rs, variant);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: read blocks failed %i", rc);
        return rc;
    }

    if (variant->track_count <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_variant: no tracks read");
        return NGX_BAD_DATA;
    }

    variant->id = id;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_segment_index(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_pckg_channel_t        *channel = obj;
    ngx_ksmp_segment_index_t  *h;

    if (channel->segment_index != NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_index: duplicate block");
        return NGX_BAD_DATA;
    }

    h = ngx_palloc(channel->pool, sizeof(*h));
    if (h == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_index: alloc failed");
        return NGX_ERROR;
    }

    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment_index: read header failed");
        return NGX_BAD_DATA;
    }

    channel->segment_index = h;

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_dynamic_var(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    uint32_t                 hash;
    ngx_pckg_channel_t      *channel = obj;
    ngx_pckg_dynamic_var_t  *var;

    var = ngx_palloc(channel->pool, sizeof(*var));
    if (var == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_dynamic_var: alloc failed");
        return NGX_ERROR;
    }

    if (ngx_mem_rstream_str_get(rs, &var->sn.str) != NGX_OK ||
        ngx_mem_rstream_str_get(rs, &var->value) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_dynamic_var: read key/value failed");
        return NGX_BAD_DATA;
    }

    hash = ngx_crc32_short(var->sn.str.data, var->sn.str.len);
    var->sn.node.key = hash;
    ngx_rbtree_insert(&channel->vars.rbtree, &var->sn.node);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_segment(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_int_t                   rc;
    ngx_uint_t                  i, n;
    ngx_flag_t                  found;
    ngx_pckg_track_t           *cur_track, *tracks;
    ngx_pckg_channel_t         *channel = obj;
    ngx_pckg_segment_t         *segment;
    ngx_ksmp_segment_header_t  *h;

    segment = ngx_pcalloc(channel->pool, sizeof(*segment));
    if (segment == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: alloc failed");
        return NGX_ERROR;
    }

    h = &segment->header;

    if (ngx_mem_rstream_read(rs, h, sizeof(*h)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: read header failed");
        return NGX_BAD_DATA;
    }

    if (h->frame_count <= 0) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: invalid frame count");
        return NGX_BAD_DATA;
    }


    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    segment->channel = channel;
    segment->pool = channel->pool;

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_SEGMENT, rs, segment);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: read blocks failed %i", rc);
        return rc;
    }


    if (segment->frames == NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: missing frame list block");
        return NGX_BAD_DATA;
    }

    if (segment->media.data == NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: missing frame data block");
        return NGX_BAD_DATA;
    }

    found = 0;

    tracks = channel->tracks.elts;
    n = channel->tracks.nelts;
    for (i = 0; i < n; i++) {

        cur_track = &tracks[i];
        if (cur_track->last_media_info->header.track_id != h->track_id) {
            continue;
        }

        if (cur_track->segment != NULL) {
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_pckg_ksmp_read_segment: track %uD already has a segment",
                cur_track->header.id);
            return NGX_BAD_DATA;
        }

        cur_track->segment = segment;
        found = 1;
    }

    if (!found) {
        ngx_log_error(NGX_LOG_WARN, rs->log, 0,
            "ngx_pckg_ksmp_read_segment: unused segment for track %uD",
            h->track_id);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_frame_list(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_str_t            frames;
    ngx_uint_t           frame_count;
    ngx_pckg_segment_t  *segment = obj;

    if (segment->frames != NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_frame_list: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    ngx_mem_rstream_get_left(rs, &frames);

    frame_count = frames.len / sizeof(segment->frames[0]);
    if (frame_count != segment->header.frame_count) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_frame_list: frame count mismatch, "
            "expected: %uD, actual: %ui",
            segment->header.frame_count, frame_count);
        return NGX_BAD_DATA;
    }

    segment->frames = ngx_palloc(segment->pool, frames.len);
    if (segment->frames == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, rs->log, 0,
            "ngx_pckg_ksmp_read_frame_list: alloc failed");
        return NGX_ERROR;
    }

    ngx_memcpy(segment->frames, frames.data, frames.len);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_frame_data(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_pckg_segment_t  *segment = obj;

    if (segment->media.data != NULL) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_frame_data: duplicate block");
        return NGX_BAD_DATA;
    }

    if (ngx_persist_read_skip_block_header(rs, header) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    ngx_mem_rstream_get_left(rs, &segment->media);

    return NGX_OK;
}


static ngx_int_t
ngx_pckg_ksmp_read_error(ngx_persist_block_hdr_t *header,
    ngx_mem_rstream_t *rs, void *obj)
{
    ngx_pckg_channel_t  *channel = obj;

    if (ngx_mem_rstream_read(rs, &channel->err_code, sizeof(channel->err_code))
        != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_error: read code failed");
        return NGX_BAD_DATA;
    }

    if (ngx_mem_rstream_str_get(rs, &channel->err_msg) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, rs->log, 0,
            "ngx_pckg_ksmp_read_error: read message failed");
        return NGX_BAD_DATA;
    }

    return NGX_OK;
}


ngx_int_t
ngx_pckg_ksmp_parse(ngx_pckg_channel_t *channel, ngx_str_t *buf,
    size_t max_size)
{
    ngx_int_t                   rc;
    ngx_uint_t                  ctx;
    ngx_mem_rstream_t           rs;
    ngx_pckg_channel_media_t   *media;
    ngx_persist_file_header_t  *h;

    rc = ngx_persist_read_file_header(buf, channel->format, channel->log,
        NULL, &rs);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse: read header failed");
        return NGX_BAD_DATA;
    }

    rc = ngx_persist_read_inflate(buf, max_size, &rs, channel->pool, NULL);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse: inflate failed %i", rc);
        return rc;
    }

    switch (channel->format) {

    case NGX_KSMP_PERSIST_TYPE:
        ctx = NGX_PCKG_KSMP_CTX_MAIN;
        break;

    case NGX_PCKG_PERSIST_TYPE_MEDIA:
        ctx = NGX_PCKG_KSMP_CTX_SGTS_MAIN;
        break;

    default:
        ngx_log_error(NGX_LOG_ERR, channel->log, 0,
            "ngx_pckg_ksmp_parse: unknown format 0x%uxD", channel->format);
        return NGX_ERROR;
    }

    rc = ngx_persist_conf_read_blocks(channel->persist, ctx, &rs, channel);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse: read blocks failed (1) %i", rc);
        return rc;
    }

    if (channel->err_code != NGX_KSMP_ERR_SUCCESS) {
        return NGX_OK;
    }

    if (channel->header.timescale == 0) {
        if (channel->format == NGX_PCKG_PERSIST_TYPE_MEDIA) {
            media = channel->media;

            ngx_log_error(NGX_LOG_ERR, channel->log, 0,
                "ngx_pckg_ksmp_parse: segment not found, "
                "index: %uD, range: %uD..%uD, track: %uD, range: %uD..%uD",
                media->segment_index, media->min_segment_index,
                media->max_segment_index, media->track_id,
                media->min_track_id, media->max_track_id);

        } else {
            ngx_log_error(NGX_LOG_ERR, channel->log, 0,
                "ngx_pckg_ksmp_parse: missing channel block");
        }

        return NGX_BAD_DATA;
    }

    h = (void *) buf->data;
    if (h->size >= buf->len) {
        return NGX_OK;
    }

    ngx_mem_rstream_set(&rs, buf->data + h->size,
        buf->data + buf->len, channel->log, NULL, h->version);

    rc = ngx_persist_conf_read_blocks(channel->persist,
        NGX_PCKG_KSMP_CTX_MAIN, &rs, channel);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_NOTICE, channel->log, 0,
            "ngx_pckg_ksmp_parse: read blocks failed (2) %i", rc);
        return rc;
    }

    return NGX_OK;
}


static ngx_persist_block_t  ngx_pckg_ksmp_blocks[] = {

    { NGX_KSMP_BLOCK_CHANNEL, NGX_PCKG_KSMP_CTX_MAIN, 0, NULL,
      ngx_pckg_ksmp_read_channel },

    { NGX_KSMP_BLOCK_TIMELINE, NGX_PCKG_KSMP_CTX_CHANNEL, 0, NULL,
      ngx_pckg_ksmp_read_timeline },

    { NGX_KSMP_BLOCK_PERIOD, NGX_PCKG_KSMP_CTX_TIMELINE, 0, NULL,
      ngx_pckg_ksmp_read_period },

    { NGX_KSMP_BLOCK_VARIANT, NGX_PCKG_KSMP_CTX_CHANNEL, 0, NULL,
      ngx_pckg_ksmp_read_variant },

    { NGX_KSMP_BLOCK_TRACK, NGX_PCKG_KSMP_CTX_VARIANT, 0, NULL,
      ngx_pckg_ksmp_read_track },

    { NGX_KSMP_BLOCK_MEDIA_INFO_QUEUE, NGX_PCKG_KSMP_CTX_TRACK, 0, NULL,
      ngx_pckg_ksmp_read_media_info_queue },

    { NGX_KSMP_BLOCK_MEDIA_INFO, NGX_PCKG_KSMP_CTX_MEDIA_INFO_QUEUE, 0, NULL,
      ngx_pckg_ksmp_read_media_info },

    { NGX_KSMP_BLOCK_TRACK_PARTS, NGX_PCKG_KSMP_CTX_TRACK, 0, NULL,
      ngx_pckg_ksmp_read_track_parts },

    { NGX_KSMP_BLOCK_SEGMENT_PARTS, NGX_PCKG_KSMP_CTX_TRACK_PARTS, 0, NULL,
      ngx_pckg_ksmp_read_segment_parts },

    { NGX_KSMP_BLOCK_RENDITION_REPORT, NGX_PCKG_KSMP_CTX_CHANNEL, 0, NULL,
      ngx_pckg_ksmp_read_rendition_reports },

    { NGX_KSMP_BLOCK_VARIANT_RR, NGX_PCKG_KSMP_CTX_RENDITION_REPORTS, 0, NULL,
      ngx_pckg_ksmp_read_rendition_report },

    { NGX_KSMP_BLOCK_SEGMENT_INFO, NGX_PCKG_KSMP_CTX_TRACK, 0, NULL,
      ngx_pckg_ksmp_read_segment_info },

    { NGX_KSMP_BLOCK_SEGMENT_INDEX, NGX_PCKG_KSMP_CTX_CHANNEL, 0, NULL,
      ngx_pckg_ksmp_read_segment_index },

    { NGX_KSMP_BLOCK_DYNAMIC_VAR, NGX_PCKG_KSMP_CTX_CHANNEL, 0, NULL,
      ngx_pckg_ksmp_read_dynamic_var },


    { NGX_KSMP_BLOCK_SEGMENT, NGX_PCKG_KSMP_CTX_MAIN, 0, NULL,
      ngx_pckg_ksmp_read_segment },

    { NGX_KSMP_BLOCK_FRAME_LIST, NGX_PCKG_KSMP_CTX_SEGMENT, 0, NULL,
      ngx_pckg_ksmp_read_frame_list },

    { NGX_KSMP_BLOCK_FRAME_DATA, NGX_PCKG_KSMP_CTX_SEGMENT, 0, NULL,
      ngx_pckg_ksmp_read_frame_data },


    { NGX_KSMP_BLOCK_ERROR, NGX_PCKG_KSMP_CTX_MAIN, 0, NULL,
      ngx_pckg_ksmp_read_error },


      ngx_null_persist_block
};


ngx_persist_conf_t *
ngx_pckg_ksmp_conf_create(ngx_conf_t *cf)
{
    ngx_persist_conf_t  *persist;

    persist = ngx_persist_conf_create(cf, NGX_PCKG_KSMP_CTX_COUNT);
    if (persist == NULL) {
        return NULL;
    }

    if (ngx_persist_conf_add_blocks(cf, persist, ngx_pckg_ksmp_blocks)
        != NGX_OK)
    {
        return NULL;
    }

    if (ngx_pckg_ksmp_sgts_add_blocks(cf, persist) != NGX_OK) {
        return NULL;
    }

    return persist;
}


ngx_int_t
ngx_pckg_ksmp_create_request(ngx_pool_t *pool, ngx_pckg_ksmp_req_t *req,
    ngx_str_t *result)
{
    u_char     *p;
    size_t      size;
    uintptr_t   channel_escape;
    uintptr_t   timeline_escape;
    uintptr_t   variants_escape;

    channel_escape = ngx_escape_uri(NULL, req->channel_id.data,
        req->channel_id.len, NGX_ESCAPE_ARGS);

    timeline_escape = ngx_escape_uri(NULL, req->timeline_id.data,
        req->timeline_id.len, NGX_ESCAPE_ARGS);

    size = sizeof("channel_id=") - 1 + req->channel_id.len + 2 * channel_escape
        + sizeof("&timeline_id=") - 1 + req->timeline_id.len
            + 2 * timeline_escape
        + sizeof("&flags=") - 1 + NGX_INT32_HEX_LEN;

    if (req->variant_ids.data != NULL) {
        variants_escape = ngx_escape_uri(NULL, req->variant_ids.data,
            req->variant_ids.len, NGX_ESCAPE_ARGS);

        size += sizeof("&variant_ids=") - 1 + req->variant_ids.len
            + 2 * variants_escape;

    } else {
        variants_escape = 0;    /* suppress warning */
    }

    if (req->time != NGX_KSMP_INVALID_TIMESTAMP) {
        size += sizeof("&time=") - 1 + NGX_INT64_LEN;
    }

    if (req->segment_index != NGX_KSMP_INVALID_SEGMENT_INDEX) {
        size += sizeof("&segment_index=") - 1 + NGX_INT32_LEN;
    }

    if (req->max_segment_index != NGX_KSMP_INVALID_SEGMENT_INDEX) {
        size += sizeof("&max_segment_index=") - 1 + NGX_INT32_LEN;
    }

    if (req->part_index != NGX_KSMP_INVALID_PART_INDEX) {
        size += sizeof("&part_index=") - 1 + NGX_INT32_LEN;
    }

    if (req->skip_boundary_percent > 0) {
        size += sizeof("&skip_boundary_percent=") - 1 + NGX_INT32_LEN;
    }


    req->media_type_mask &= KMP_MEDIA_TYPE_MASK;
    if ((req->media_type_mask & KMP_MEDIA_TYPE_MASK) ==
        KMP_MEDIA_TYPE_MASK)
    {
        req->media_type_mask = 0;

    } else {
        size += sizeof("&media_type_mask=") - 1 + NGX_INT32_HEX_LEN;
    }

    if (req->padding) {
        size += sizeof("&padding=") - 1 + NGX_SIZE_T_LEN;
    }

    p = ngx_pnalloc(pool, size);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, pool->log, 0,
            "ngx_pckg_ksmp_create_request: alloc failed");
        return NGX_ERROR;
    }

    result->data = p;

    p = ngx_copy_fix(p, "channel_id=");
    if (channel_escape) {
        p = (u_char *) ngx_escape_uri(p, req->channel_id.data,
            req->channel_id.len, NGX_ESCAPE_ARGS);

    } else {
        p = ngx_copy_str(p, req->channel_id);
    }

    p = ngx_copy_fix(p, "&timeline_id=");
    if (timeline_escape) {
        p = (u_char *) ngx_escape_uri(p, req->timeline_id.data,
            req->timeline_id.len, NGX_ESCAPE_ARGS);

    } else {
        p = ngx_copy_str(p, req->timeline_id);
    }

    p = ngx_copy_fix(p, "&flags=");
    p = ngx_sprintf(p, "%uxD", req->flags);

    if (req->variant_ids.data != NULL) {
        p = ngx_copy_fix(p, "&variant_ids=");
        if (variants_escape) {
            p = (u_char *) ngx_escape_uri(p, req->variant_ids.data,
                req->variant_ids.len, NGX_ESCAPE_ARGS);

        } else {
            p = ngx_copy_str(p, req->variant_ids);
        }
    }

    if (req->time != NGX_KSMP_INVALID_TIMESTAMP) {
        p = ngx_copy_fix(p, "&time=");
        p = ngx_sprintf(p, "%L", req->time);
    }

    if (req->segment_index != NGX_KSMP_INVALID_SEGMENT_INDEX) {
        p = ngx_copy_fix(p, "&segment_index=");
        p = ngx_sprintf(p, "%uD", req->segment_index);
    }

    if (req->max_segment_index != NGX_KSMP_INVALID_SEGMENT_INDEX) {
        p = ngx_copy_fix(p, "&max_segment_index=");
        p = ngx_sprintf(p, "%uD", req->max_segment_index);
    }

    if (req->part_index != NGX_KSMP_INVALID_PART_INDEX) {
        p = ngx_copy_fix(p, "&part_index=");
        p = ngx_sprintf(p, "%uD", req->part_index);
    }

    if (req->skip_boundary_percent > 0) {
        p = ngx_copy_fix(p, "&skip_boundary_percent=");
        p = ngx_sprintf(p, "%uD", req->skip_boundary_percent);
    }


    if (req->media_type_mask) {
        p = ngx_copy_fix(p, "&media_type_mask=");
        p = ngx_sprintf(p, "%uxD", req->media_type_mask);
    }

    if (req->padding) {
        p = ngx_copy_fix(p, "&padding=");
        p = ngx_sprintf(p, "%uz", req->padding);
    }

    result->len = p - result->data;

    if (result->len > size) {
        ngx_log_error(NGX_LOG_ALERT, pool->log, 0,
            "ngx_pckg_ksmp_create_request: "
            "result length %uz greater than allocated length %uz",
            result->len, size);
        return NGX_ERROR;
    }

    return NGX_OK;
}
