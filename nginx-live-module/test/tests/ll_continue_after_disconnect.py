from test_base import *

def updateConf(conf):
    block = getConfBlock(conf, ['live', 'preset ll'])
    block.append(['ll_segmenter_inactive_timeout', '100s'])

# EXPECTED:
#   20 sec audio + video, with some short freeze around 7 sec

def stream(nl, duration, eos):
    rv = KmpMediaFileReader(TEST_VIDEO1, 0)
    ra = KmpMediaFileReader(TEST_VIDEO1, 1)
    rs = KmpSRTReader(TEST_VIDEO1_CC_ENG)

    st = KmpSendTimestamps()

    sv, sa = createVariant(nl, 'var1', [('v1', 'video'), ('a1', 'audio')])
    ss = createSubtitleVariant(nl, 'sub1', 's1', 'English', 'eng')

    kmpSendStreams([
        (rv, sv),
        (ra, sa),
        (rs, ss),
    ], st, duration)

    if eos:
        kmpSendEndOfStream([sv, sa, ss])
    else:
        time.sleep(2)
        sv.close()
        sa.close()
        ss.close()

def test(channelId=CHANNEL_ID):
    nl = setupChannelTimeline(channelId, preset=LL_PRESET)

    stream(nl, 7, False)
    stream(nl, 20, True)

    time.sleep(1)
    nl.timeline.update(NginxLiveTimeline(id=TIMELINE_ID, end_list='on'))

    testLLDefaultStreams(channelId, __file__)

    logTracker.assertContains(b'ngx_live_lls_track_flush_segment: index: 1')
    logTracker.assertNotContains(b'ngx_live_lls_track_dispose_all: disposing')
