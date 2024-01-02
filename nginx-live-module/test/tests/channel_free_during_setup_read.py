from test_base import *

def updateConf(conf):
    appendConfDirective(conf, ['http', 'server'], [['location', '/store/channel/test/setup'], [['proxy_pass', 'http://127.0.0.1:8002']]])

def test(channelId=CHANNEL_ID):
    nl = nginxLiveClient()
    TcpServer(8002, lambda s: nl.channel.delete(channelId))
    try:
        nl.channel.create(NginxLiveChannel(id=channelId, preset='main'))
        assert(False)
    except requests.exceptions.HTTPError as e:
        if e.response.status_code != 409:
            raise

    logTracker.assertContains(b'ngx_live_persist_core_read_handler: read failed 409')

    cleanupStack.reset()
    time.sleep(1)
