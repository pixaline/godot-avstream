extends TextureRect
class_name UIStreamViewer

export(String) var streamUrl = ""

onready var streamAudio = self.get_node("%StreamAudioPlayer")

var stream  = FFAVStream.new()
var decoder = FFAVStreamDecoder.new()

var _reconnect_timer := 0.0
var _reconnect_times := 1
var _reconnecting := false

signal on_stream_texture(texture)
signal on_stream_audio_data(data)
signal on_stream_start()
signal on_stream_stop()

func _ready():
	if (streamAudio is AudioStreamPlayer):
		var gen = AudioStreamGenerator.new()
		gen.mix_rate = 48000
		gen.buffer_length = 0.5
		streamAudio.stream = gen

	var arch = ""
	if OS.has_feature("64"): arch = "_64"
	if OS.has_feature("32"): arch = "_32"
	
	var libpath := "user://libs/ffmpeg/"
	if OS.get_name() == "Windows":
		libpath += "windows%s/" % [arch]
	elif OS.get_name() == "X11":
		libpath += "x11%s/" % [arch]
	elif OS.get_name() == "OSX":
		libpath += "osx%s/" % [arch]
	elif OS.get_name() == "Android":
		libpath += "android%s/" % [arch]
		
	stream.set_library_path(libpath)
	stream.load_libraries()
	
	decoder.set_stream(stream)
	
	decoder.connect("audio_frame", self, "_on_audio_frame")

	stream.connect("stream_opened", self, "_on_stream_opened")
	stream.connect("stream_closed", self, "_on_stream_closed")

	
	stream.play(streamUrl)
	decoder.start()

func has_missing_libraries() -> bool:
	return (stream.get_state() == FFAVStream.STATE_MISSING_LIBS)

func get_stream_audio_generator():
	return streamAudio.stream

func _process(delta : float):
	decoder.update()
	
	var tex = decoder.get_texture()
	if tex != null and self.texture != tex:
		self.texture = tex
		emit_signal("on_stream_texture", tex)

	if _reconnecting:
		_reconnect_timer -= delta
		if _reconnect_timer <= 0.0:
			_reconnecting = false
			stream.play(streamUrl)
			decoder.start()

func stop_stream():
	stream.stop()

func try_reconnect():
	_reconnecting = true
	_reconnect_times = 1
	_reconnect_timer = get_reconnect_timeout()

func _on_stream_opened():
	emit_signal("on_stream_start")
	
	_reconnecting = false
	_reconnect_times = 0

func _on_stream_closed(state):
	emit_signal("on_stream_stop")
	
	decoder.stop()
	if state == FFAVStream.STATE_ERROR:
		_reconnecting = true
		_reconnect_timer = get_reconnect_timeout()
		if _reconnect_times < 10:
			_reconnect_times += 1

func get_reconnect_timeout() -> float:
	return max(_reconnect_times - 1, 0) * 3.0

func _on_audio_frame(data: PoolVector2Array):
	var playback = streamAudio.get_stream_playback()
	
	if playback.can_push_buffer(data.size()):
		playback.push_buffer(data)

	if not streamAudio.playing:
		streamAudio.playing = true
	
	emit_signal("on_stream_audio_data", data)
