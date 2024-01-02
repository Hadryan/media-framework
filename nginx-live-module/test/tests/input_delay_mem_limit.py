from test_base import *

def test(channelId=CHANNEL_ID):
    st = KmpSendTimestamps()
    st.created = int(time.time() * 90000)

    nl = setupChannelTimeline(channelId)
    nl.channel.update(NginxLiveChannel(id=channelId, input_delay=599999, mem_limit=24*1024*1024))

    sv, sa = createVariant(nl, 'var1', [('v1', 'video'), ('a1', 'audio')])

    kmpSendStreams([
        (KmpMediaFileReader(TEST_VIDEO_HIGH, 0), sv),
        (KmpMediaFileReader(TEST_VIDEO_HIGH, 1), sa),
    ], st, 30, realtime=1)

    kmpSendEndOfStream([sv, sa])

    time.sleep(5)

    nl.timeline.update(NginxLiveTimeline(id=TIMELINE_ID, end_list='on'))

    # Note: not comparing the full stream due to the use of a real 'created' value
    res = requests.get(url=getStreamUrl(channelId, 'hls-ts', 'index-svar1.m3u8'))
    assertEndsWith(res.content.rstrip(), b'seg-5-svar1.ts\n#EXT-X-ENDLIST')

    logTracker.assertContains(b'ngx_live_segmenter_channel_watermark: reducing input delay')
