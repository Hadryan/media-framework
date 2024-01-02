from test_base import *

def updateConf(conf):
    appendConfDirective(conf, ['http', 'server'], [['location', '/store/channel/test/index'], [['proxy_pass', 'http://127.0.0.1:8002']]])

def test(channelId=CHANNEL_ID):
    st = KmpSendTimestamps()

    nl = setupChannelTimeline(channelId)

    rv = KmpMediaFileReader(TEST_VIDEO1, 0)
    ra = KmpMediaFileReader(TEST_VIDEO1, 1)

    sv, sa = createVariant(nl, 'var1', [('v1', 'video'), ('a1', 'audio')])

    TcpServer(8002, lambda s: nl.channel.delete(channelId))

    try:
        kmpSendStreams([
            (rv, sv),
            (ra, sa),
        ], st, 30, realtime=False)
    except socket.error:
        pass

    time.sleep(.5)

    logTracker.assertContains(b'ngx_live_persist_index_channel_free: cancelling write')

    cleanupStack.reset()
    time.sleep(1)
