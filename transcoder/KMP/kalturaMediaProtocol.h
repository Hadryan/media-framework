#ifndef _LIVE_PROTOCOL_H_INCLUDED_
#define _LIVE_PROTOCOL_H_INCLUDED_


typedef enum {
    KMP_CODEC_VIDEO_JPEG = 1,
    KMP_CODEC_VIDEO_SORENSON_H263 = 2,
    KMP_CODEC_VIDEO_SCREEN = 3,
    KMP_CODEC_VIDEO_ON2_VP6 = 4,
    KMP_CODEC_VIDEO_ON2_VP6_ALPHA = 5,
    KMP_CODEC_VIDEO_SCREEN2 = 6,
    KMP_CODEC_VIDEO_H264 = 7,
    KMP_CODEC_VIDEO_HEVC = 8,
    KMP_CODEC_VIDEO_H265 = KMP_CODEC_VIDEO_HEVC,

    KMP_CODEC_AUDIO_UNCOMPRESSED = 1016,
    KMP_CODEC_AUDIO_ADPCM = 1001,
    KMP_CODEC_AUDIO_MP3 = 1002,
    KMP_CODEC_AUDIO_LINEAR_LE = 1003,
    KMP_CODEC_AUDIO_NELLY16 = 1004,
    KMP_CODEC_AUDIO_NELLY8 = 1005,
    KMP_CODEC_AUDIO_NELLY = 1006,
    KMP_CODEC_AUDIO_G711A = 1007,
    KMP_CODEC_AUDIO_G711U = 1008,
    KMP_CODEC_AUDIO_AAC = 1010,
    KMP_CODEC_AUDIO_SPEEX = 1011,
    KMP_CODEC_AUDIO_MP3_8 = 1014,
    KMP_CODEC_AUDIO_DEVSPEC = 1015,
} kmp_codec_id;

typedef struct {
    uint32_t num;
    uint32_t denom;
} kmp_rational_t;

typedef struct {
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t sample_rate;
    uint64_t channel_layout;
} kmp_audio_media_info_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    kmp_rational_t frame_rate;        // currently rounded by nginx-rtmp, will need a patch to avoid it
    uint32_t cea_captions;
} kmp_video_media_info_t;

typedef struct {
    uint32_t media_type;    // 0 = video, 1 = audio
    uint32_t codec_id;        // currently rtmp enum
    uint32_t timescale;        // currently hardcoded to 90k, maybe for audio we should use the sample rate
    uint32_t bitrate;        // bps    (rtmp module returns in kbps, will multiply by 1000)
    union {
        kmp_video_media_info_t video;
        kmp_audio_media_info_t audio;
    } u;
} kmp_media_info_t;

typedef struct {
    int64_t created;
    int64_t dts;
    uint32_t flags;
    int32_t pts_delay;
} kmp_frame_t;

#define KMP_FRAME_FLAG_KEY (0x01)
enum {
    // client -> server
    KMP_PACKET_CONNECT = 0x74636e63,    // cnct
    KMP_PACKET_MEDIA_INFO = 0x666e696d, // minf
    KMP_PACKET_FRAME = 0x6d617266,      // fram
    KMP_PACKET_EOS = 0x74736f65,         // eost

    // server -> client
    KMP_PACKET_ACK_FRAMES = 0x666b6361   // ackf
};

#define KMP_MAX_CHANNEL_ID (32)
#define KMP_MAX_TRACK_ID (32)


enum {
    KMP_MEDIA_VIDEO,
    KMP_MEDIA_AUDIO,
    KMP_MEDIA_COUNT,
};

typedef struct {
    uint32_t packet_type;
    uint32_t header_size;
    uint32_t data_size;
    uint32_t reserved;
} kmp_packet_header_t;

typedef uint64_t frame_id_t;


typedef struct {
    kmp_packet_header_t header;
    u_char channel_id[KMP_MAX_CHANNEL_ID];
    u_char track_id[KMP_MAX_TRACK_ID];
    frame_id_t initial_frame_id;
    frame_id_t initial_transcoded_frame_id;
    uint32_t offset;
    uint32_t padding;
} kmp_connect_packet_t;

typedef struct {
    kmp_packet_header_t header;
    kmp_media_info_t m;
} kmp_media_info_packet_t;

typedef struct {
    kmp_packet_header_t header;
    kmp_frame_t f;
} kmp_frame_packet_t;

typedef struct {
    kmp_packet_header_t header;
    frame_id_t frame_id;
    frame_id_t transcoded_frame_id;
    uint32_t offset;
    uint32_t padding;
} kmp_ack_frames_packet_t;

#endif /* _LIVE_PROTOCOL_H_INCLUDED_ */
