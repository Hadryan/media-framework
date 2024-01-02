# Nginx MPEGTS -> KMP Module

Publishes incoming MPEGTS streams to one or more destinations using the KMP (Kaltura Media Protocol) protocol.

Dependencies: [nginx-common](../nginx-common/), [nginx-kmp-out-module](../nginx-kmp-out-module/), [nginx-mpegts-module](../nginx-mpegts-module/).


## Configuration

### Sample Configuration

```nginx
# MPEG-TS/SRT input
srt {
    server {
        listen 7045;

        proxy_pass tcp://127.0.0.1:8002;
        proxy_header '$stream_id\n';
    }
}

# MPEG-TS/TCP input
stream {
    server {
        listen 8002;

        preread_str_delim '\n';

        ts;
        ts_stream_id $preread_str;

        ts_kmp on;
        ts_kmp_ctrl_connect_url http://127.0.0.1:8001/control/;
        ts_kmp_ctrl_publish_url http://127.0.0.1:8001/control/;
        ts_kmp_ctrl_unpublish_url http://127.0.0.1:8001/control/;
        ts_kmp_ctrl_republish_url http://127.0.0.1:8001/control/;
    }
}

http {
    server {
        listen 80;

        # MPEG-TS/HTTP input
        location /publish/ {
            client_max_body_size 0;

            ts;
            ts_stream_id $arg_streamid;

            ts_kmp on;
            ts_kmp_ctrl_connect_url http://127.0.0.1:8001/control/;
            ts_kmp_ctrl_publish_url http://127.0.0.1:8001/control/;
            ts_kmp_ctrl_unpublish_url http://127.0.0.1:8001/control/;
            ts_kmp_ctrl_republish_url http://127.0.0.1:8001/control/;
        }

        location /ts_kmp_api/ {
            allow 127.0.0.1/32;
            deny all;

            ts_kmp_api write=on;
        }
    }
}
```

### Configuration Directives

#### ts_kmp
* **syntax**: `ts_kmp;`
* **default**: `none`
* **context**: `location`, `stream/server`

Enables the media interface of this module in the surrounding stream-server/location block.
The `ts;` directive of nginx-mpegts-module should also be used in the same context, otherwise this directive has no effect.

#### ts_kmp_ctrl_connect_url
* **syntax**: `ts_kmp_ctrl_connect_url url;`
* **default**: `none`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the URL of the HTTP `connect` callback. The request is issued asynchronously when the first MPEG-TS PAT packet is received.
Sending of `publish` requests is delayed until the `connect` request completes, incoming video / audio frames are buffered during this time.
If the `connect` response does not include a `code` field with a value of `ok`, the HTTP/TCP connection is dropped.

Sample request body:
```json
{
    "event_type": "connect",
    "input_type": "mpegts",
    "mpegts": {
        "stream_id": "ch1_st1",
        "addr": "127.0.0.1",
        "connection": 33
    }
}
```

Sample response:
```json
{
    "code": "ok",
    "message": "Success"
}
```

#### ts_kmp_ctrl_publish_url
* **syntax**: `ts_kmp_ctrl_publish_url url;`
* **default**: `none`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the URL of the HTTP `publish` callback, called for each track (audio/video) that is published to the server.

Sample request body:
```json
{
    "event_type": "publish",
    "input_id": "mpegts://ch1_st1_257",
    "input_type": "mpegts",
    "mpegts": {
        "stream_id": "ch1_st1",
        "pid": 257,
        "index": 1,
        "prog_num": 1,
        "addr": "127.0.0.1",
        "connection": 33
    },
    "media_info": {
        "media_type": "audio",
        "bitrate": 0,
        "codec_id": 1010,
        "extra_data": "1210",
        "channels": 2,
        "channel_layout": "3",
        "bits_per_sample": 16,
        "sample_rate": 44100
    }
}
```

See [Publish](../nginx-kmp-out-module/README.md#publish) for more details on the `publish` request.

#### ts_kmp_ctrl_unpublish_url
* **syntax**: `ts_kmp_ctrl_unpublish_url url;`
* **default**: `none`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the URL of the HTTP `unpublish` callback, called whenever a track (audio/video) stops being published to the server, or an unrecoverable error occurs when sending to upstream.
The response of this notification is ignored, and no retries are performed in case of error.

See [Unpublish](../nginx-kmp-out-module/README.md#unpublish) for more details on the `unpublish` request.

#### ts_kmp_ctrl_republish_url
* **syntax**: `ts_kmp_ctrl_republish_url url;`
* **default**: `none`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the URL of the HTTP `republish` callback, called in case of an error/disconnect on some upstream KMP connection.
The upstream server can use this event to provide the module with a new KMP endpoint to publish to.

See [Republish](../nginx-kmp-out-module/README.md#republish) for more details on the `republish` request.

#### ts_kmp_ctrl_add_header
* **syntax**: `ts_kmp_ctrl_add_header name value;`
* **default**: `none`
* **context**: `http`, `server`, `location`, `stream`, `server`

Adds a request header to all control requests (`publish`, `unpublish` etc.).
There could be several `ts_kmp_ctrl_add_header` directives.
These directives are inherited from the previous level if and only if there are no `ts_kmp_ctrl_add_header` directives defined on the current level.

#### ts_kmp_ctrl_timeout
* **syntax**: `ts_kmp_ctrl_timeout msec;`
* **default**: `2s`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets a timeout for sending HTTP requests to the upstream server.
The timeout includes both the connection establishment as well as the sending of the request.

#### ts_kmp_ctrl_read_timeout
* **syntax**: `ts_kmp_ctrl_read_timeout msec;`
* **default**: `20s`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets a timeout for reading the response of HTTP requests sent to the upstream server.

#### ts_kmp_ctrl_buffer_size
* **syntax**: `ts_kmp_ctrl_buffer_size size;`
* **default**: `4k`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the size of the buffer that holds the response of the HTTP requests.
The buffer size should be large enough to hold the largest expected response.

#### ts_kmp_ctrl_retries
* **syntax**: `ts_kmp_ctrl_retries num;`
* **default**: `5`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the number of retries for issuing HTTP requests. A request is considered as failed if -
- The request could not be sent (e.g. connect error)
- The response could not be parsed as JSON (bad http status, non-json content type, invalid JSON)
- The `code` field is missing/has a value other than `ok`

#### ts_kmp_ctrl_retry_interval
* **syntax**: `ts_kmp_ctrl_retry_interval msec;`
* **default**: `2s`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the time to wait before performing each retry attempt for HTTP requests.

#### ts_kmp_timescale
* **syntax**: `ts_kmp_timescale num;`
* **default**: `90000`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the timescale that is reported in the KMP media info packet.
Timestamp scaling is currently not implemented, so this directive should not be changed from the default MPEG-TS timescale.

#### ts_kmp_timeout
* **syntax**: `ts_kmp_timeout msec;`
* **default**: `10s`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the timeout for sending data to the upstream KMP server.

#### ts_kmp_max_free_buffers
* **syntax**: `ts_kmp_max_free_buffers num;`
* **default**: `4`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the maximum number of free buffers that are kept after receiving acks from the upstream server.
A large value may save some memory alloc/free operations, but can also increase memory usage.

#### ts_kmp_buffer_bin_count
* **syntax**: `ts_kmp_buffer_bin_count num;`
* **default**: `8`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the number of bins that are used to group the video/audio buffers.
The buffers are grouped in bins according to the number of allocated blocks they contain.
When allocating a block, the allocator prefers to use buffers that are more utilized.
This is done in order to concentrate the allocated blocks in fewer buffers, and enable
the allocator to free unused buffers.

#### ts_kmp_mem_high_watermark
* **syntax**: `ts_kmp_mem_high_watermark percent;`
* **default**: `75`
* **context**: `http`, `server`, `location`, `stream`, `server`

A memory utilization threshold, expressed as a percent of the memory limit.
If the high watermark is reached, the module starts releasing video/audio buffers
of frames that were not acknowledged. The process stops when the memory usage
drops below the low watermark threshold.

#### ts_kmp_mem_low_watermark
* **syntax**: `ts_kmp_mem_low_watermark percent;`
* **default**: `50`
* **context**: `http`, `server`, `location`, `stream`, `server`

See the description of `ts_kmp_mem_high_watermark` above.

#### ts_kmp_video_buffer_size
* **syntax**: `ts_kmp_video_buffer_size size;`
* **default**: `64k`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the size of the buffers used to send video data to the upstream server.
A large value can be more efficient, but increases the latency (a buffer is sent either when it's full or the flush timeout expires).

#### ts_kmp_video_mem_limit
* **syntax**: `ts_kmp_video_mem_limit size;`
* **default**: `32m`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the maximum total size of the buffers used to send video data to the upstream server.
If the limit is hit, the module drops the HTTP/TCP connection.

#### ts_kmp_audio_buffer_size
* **syntax**: `ts_kmp_audio_buffer_size size;`
* **default**: `4k`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the size of the buffers used to send audio data to the upstream server.
A large value can be more efficient, but increases the latency (a buffer is sent either when it's full or the flush timeout expires).

#### ts_kmp_audio_mem_limit
* **syntax**: `ts_kmp_audio_mem_limit size;`
* **default**: `1m`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the maximum total size of the buffers used to send audio data to the upstream server.
If the limit is hit, the module drops the HTTP/TCP connection.

#### ts_kmp_flush_timeout
* **syntax**: `ts_kmp_flush_timeout msec;`
* **default**: `1s`
* **context**: `http`, `server`, `location`, `stream`, `server`

Sets the timeout for flushing buffered data to the upstream KMP server.
KMP data is kept in buffers of size ts_kmp_xxx_buffer_size, a buffer is sent when it becomes full, or when the flush timeout expires.

#### ts_kmp_log_frames
* **syntax**: `ts_kmp_log_frames all | key | off;`
* **default**: `off`
* **context**: `http`, `server`, `location`, `stream`, `server`

When enabled, the module logs the metadata of every frame that is sent -
1. KMP frame header - created, dts, flags, pts delay
2. Data size and MD5 hash

The value `key` can be used to log only the metadata of video keyframes.

#### ts_kmp_republish_interval
* **syntax**: `ts_kmp_republish_interval msec;`
* **default**: `1s`
* **context**: `http`, `server`, `location`, `stream`, `server`

The minimum time that should pass between `republish` requests, in milliseconds.

#### ts_kmp_max_republishes
* **syntax**: `ts_kmp_max_republishes num;`
* **default**: `15`
* **context**: `http`, `server`, `location`, `stream`, `server`

The maximum number of consecutive `republish` requests that can be sent before giving up.
If more than `ts_kmp_republish_interval` seconds passed since the last `republish`, the counter is reset.

#### ts_kmp_api
* **syntax**: `ts_kmp_api [write=on|off];`
* **default**: `none`
* **context**: `location`

Enables the API interface of this module in the surrounding location block. Access to this location should be limited.

The optional `write` parameter determines whether the API is read-only or read-write. By default, the API is read-only.


## API Objects

The sections below list the possible fields in each type of API object.

### Global Scope

- `version` - string, nginx-mpegts-kmp-module version
- `nginx_version` - string, nginx version
- `compiler` - string, the compiler used to build nginx-mpegts-kmp-module
- `built` - string, the time nginx-mpegts-kmp-module was built
- `pid` - integer, the nginx process id
- `uptime` - integer, the time since the nginx worker was started, in seconds
- `sessions` - array of objects, each object is a [Session Object](#session-object)

### Session Object

- `remote_addr` - string, the ip + port of the remote peer
- `local_addr` - string, the local ip + port of the connection
- `connection` - integer, the nginx connection identifier, unique per nginx worker process
- `uptime` - integer, the time that passed since the connection was established, in seconds
- `stream_id` - string, the value that was set using the `ts_stream_id` directive
- `tracks` - array of objects, each object is a [Track Object](../nginx-kmp-out-module/README.md#track-object).


## API Endpoints

### GET /

Get the full status JSON.

Possible status codes:
- 200 - Success, returns a JSON object

### DELETE /sessions/{connection}

Drop an MPEG-TS session by connection id.

Possible status codes:
- 204 - Success, connection was dropped
- 400 - The provided connection id is not a number
- 404 - No session matching the provided connection id was found
