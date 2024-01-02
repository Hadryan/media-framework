from test_base import *

def updateConf(conf):
    appendConfDirective(conf, ['http', 'server'], [['location', '/store/channel/test/setup'], [['proxy_pass', 'http://127.0.0.1:8002']]])

def test(channelId=CHANNEL_ID):
    nl = nginxLiveClient()
    TcpServer(8002, lambda s: nl.channel.delete(channelId))

    nl.channel.create(NginxLiveChannel(id=channelId, preset='main', read=False))
    nl.setChannelId(channelId)
    nl.timeline.create(NginxLiveTimeline(id=TIMELINE_ID, active=True))

    while True:
        try:
            nl.channel.get(channelId)
        except requests.exceptions.HTTPError as e:
            if e.response.status_code != 404:
                raise
            break

        time.sleep(.1)

    logTracker.assertContains(b'ngx_live_persist_setup_channel_free: cancelling write')

    cleanupStack.reset()
    time.sleep(1)
