from test_base import *

def updateConf(conf):
    block = getConfBlock(conf, ['live', 'preset ll'])
    block.append(['ll_segmenter_max_pending_segments', '2'])
    block.append(['ll_segmenter_close_segment_delay', '10s'])

# EXPECTED:
#   20 sec audio + video

def test(channelId=CHANNEL_ID):
    st = KmpSendTimestamps()

    nl = setupChannelTimeline(channelId, preset=LL_PRESET)

    rv = KmpMediaFileReader(TEST_VIDEO1, 0)
    ra = KmpMediaFileReader(TEST_VIDEO1, 1)
    rs = KmpSRTReader(TEST_VIDEO1_CC_ENG)

    sv, sa = createVariant(nl, 'var1', [('v1', 'video'), ('a1', 'audio')])
    ss = createSubtitleVariant(nl, 'sub1', 's1', 'English', 'eng')

    kmpSendStreams([
        (rv, sv),
        (ra, sa),
        (rs, ss),
    ], st, 20)

    kmpSendEndOfStream([sv, sa, ss])

    nl.timeline.update(NginxLiveTimeline(id=TIMELINE_ID, end_list='on'))

    time.sleep(15)

    testLLDefaultStreams(channelId, __file__)

    logTracker.assertContains(b'ngx_live_lls_force_close_segment: forcing close, started_tracks: 0')
