import requests
try:
    from urllib.parse import quote  # python 3
except ImportError:
    from urllib import quote        # python 2

class NginxLiveNull:
    pass

class NginxLive:
    def __init__(self, url):
        self.url = url
        self.channel = NginxLiveChannelService(self)
        self.variant = NginxLiveVariantService(self)
        self.track = NginxLiveTrackService(self)
        self.timeline = NginxLiveTimelineService(self)

    def get(self, path):
        req = requests.get(url=self.url + path)
        req.raise_for_status()
        return req.json()

    def delete(self, path):
        req = requests.delete(url=self.url + path)
        req.raise_for_status()

    @staticmethod
    def mapParams(params):
        res = {}
        for k, v in params.items():
            if v is None:
                continue
            if isinstance(v, NginxLiveNull):
                v = None
            res[k] = v
        return res

    def post(self, path, params):
        req = requests.post(url=self.url + path, json=self.mapParams(params))
        req.raise_for_status()
        if len(req.text) == 0:
            return None
        return req.json()

    def put(self, path, params):
        req = requests.put(url=self.url + path, json=self.mapParams(params))
        req.raise_for_status()
        if len(req.text) == 0:
            return None
        return req.json()

    def getPath(self, *params):
        params = map(lambda x: quote(x, safe=''), params)
        return '/' + '/'.join(params)

    def setChannelId(self, id):
        self.channelId = id

    def getChannelPath(self, *params):
        return self.getPath('channels', self.channelId, *params)

class NginxLiveChannel:
    def __init__(self, id=None, preset=None, opaque=None, segment_duration=None, input_delay=None, filler=None, read=None, vars=None, initial_segment_index=None, mem_limit=None):
        self.id = id
        self.preset = preset
        self.opaque = opaque
        self.segment_duration = segment_duration
        self.input_delay = input_delay
        self.filler = filler.__dict__ if filler is not None else None
        self.read = read
        self.vars = vars
        self.initial_segment_index = initial_segment_index
        self.mem_limit = mem_limit

class NginxLiveFiller:
    def __init__(self, channel_id=None, preset=None, timeline_id=None, save=None):
        self.channel_id = channel_id
        self.preset = preset
        self.timeline_id = timeline_id
        self.save = save

class NginxLiveChannelService:
    def __init__(self, base):
        self.base = base

    def getAll(self):
        return self.base.get(
            self.base.getPath('channels'))

    def get(self, id):
        return self.base.get(
            self.base.getPath('channels', id))

    def delete(self, id):
        return self.base.delete(
            self.base.getPath('channels', id))

    def create(self, channel):
        return self.base.post(
            self.base.getPath('channels'),
            channel.__dict__)

    def update(self, channel):
        return self.base.put(
            self.base.getPath('channels', channel.id),
            channel.__dict__)

class NginxLiveVariant:
    def __init__(self, id=None, opaque=None, label=None, lang=None, role=None, is_default=None, track_ids=None):
        self.id = id
        self.opaque =opaque
        self.label = label
        self.lang = lang
        self.role = role
        self.is_default = is_default
        self.track_ids = track_ids

class NginxLiveVariantService:
    def __init__(self, base):
        self.base = base

    def getAll(self):
        return self.base.get(
            self.base.getChannelPath('variants'))

    def delete(self, id):
        return self.base.delete(
            self.base.getChannelPath('variants', id))

    def create(self, variant):
        return self.base.post(
            self.base.getChannelPath('variants'),
            variant.__dict__)

    def addTrack(self, variantId, trackId):
        return self.base.post(
            self.base.getChannelPath('variants', variantId, 'tracks'),
            {'id': trackId})

class NginxLiveTrack:
    def __init__(self, id=None, opaque=None, media_type=None, group_id=None):
        self.id = id
        self.opaque =opaque
        self.media_type = media_type
        self.group_id = group_id

class NginxLiveTrackService:
    def __init__(self, base):
        self.base = base

    def getAll(self):
        return self.base.get(
            self.base.getChannelPath('tracks'))

    def delete(self, id):
        return self.base.delete(
            self.base.getChannelPath('tracks', id))

    def create(self, track):
        return self.base.post(
            self.base.getChannelPath('tracks'),
            track.__dict__)

    def update(self, track):
        return self.base.put(
            self.base.getChannelPath('tracks', track.id),
            track.__dict__)

class NginxLiveTimeline:
    def __init__(self, id=None, source=None, active=None, period_gap=None, max_segments=None, max_duration=None, start=None, end=None, manifest_max_segments=None, manifest_max_duration=None, manifest_expiry_threshold=None, manifest_target_duration_segments=None, no_truncate=None, end_list=None):
        self.id = id
        self.source = source.__dict__ if source is not None else None
        self.active = active
        self.period_gap = period_gap
        self.max_segments = max_segments
        self.max_duration = max_duration
        self.start = start
        self.end = end
        self.manifest_max_segments = manifest_max_segments
        self.manifest_max_duration = manifest_max_duration
        self.manifest_expiry_threshold = manifest_expiry_threshold
        self.manifest_target_duration_segments = manifest_target_duration_segments
        self.no_truncate = no_truncate
        self.end_list = end_list

class NginxLiveTimelineSource:
    def __init__(self, id=None, start_offset=None, end_offset=None):
        self.id = id
        self.start_offset = start_offset
        self.end_offset = end_offset

class NginxLiveTimelineService:
    def __init__(self, base):
        self.base = base

    def getAll(self):
        return self.base.get(
            self.base.getChannelPath('timelines'))

    def get(self, id):
        return self.base.get(
            self.base.getChannelPath('timelines', id))

    def delete(self, id):
        return self.base.delete(
            self.base.getChannelPath('timelines', id))

    def create(self, timeline):
        return self.base.post(
            self.base.getChannelPath('timelines'),
            timeline.__dict__)

    def update(self, timeline):
        return self.base.put(
            self.base.getChannelPath('timelines', timeline.id),
            timeline.__dict__)
