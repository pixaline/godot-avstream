#include "av_stream.h"
#include "core/print_string.h"
#include "core/os/os.h"
#include "core/os/file_access.h"
#include "core/project_settings.h"

// ---------------------------------------------------------------------------
// Statics
// ---------------------------------------------------------------------------
FFmpegFunctions g_ff;
String  FFAVStream::_library_path   = "user://lib/ffmpeg";  // sensible default
void   *FFAVStream::_handle_avutil     = nullptr;
void   *FFAVStream::_handle_avcodec    = nullptr;
void   *FFAVStream::_handle_avformat   = nullptr;
void   *FFAVStream::_handle_swresample = nullptr;
void   *FFAVStream::_handle_winpthread = nullptr;

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

void FFAVStream::set_library_path(const String &p_path) {
    _library_path = p_path;
}

String FFAVStream::get_library_path() {
    return _library_path;
}

// Converts a Godot path (user://, res://) or absolute path to a real OS path.
String FFAVStream::_resolve_path(const String &p_path) {
    if (p_path.begins_with("user://")) {
        return ProjectSettings::get_singleton()->globalize_path(p_path);
    }
    if (p_path.begins_with("res://")) {
        return ProjectSettings::get_singleton()->globalize_path(p_path);
    }
    // Already an absolute path
    return p_path;
}

// ---------------------------------------------------------------------------
// Symbol loader
// ---------------------------------------------------------------------------
bool FFAVStream::_load_symbol(void *handle, const char *name, void *&out_ptr) {
    Error err = OS::get_singleton()->get_dynamic_library_symbol_handle(
        handle, name, out_ptr, false);
    if (err != OK) {
        print_error(String("FFAVStream: missing symbol: ") + name);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// load_libraries()
// Uses whatever path is currently set via set_library_path().
// ---------------------------------------------------------------------------
bool FFAVStream::load_libraries() {
    if (g_ff.is_loaded()) return true;

    String dir = _resolve_path(_library_path);

    // Ensure trailing separator
    if (!dir.ends_with("/") && !dir.ends_with("\\")) {
        dir += "/";
    }

#if defined(WINDOWS_ENABLED)
    // Load winpthread first so Windows can resolve it when avutil loads
    print_line(String("FFAVStream: loading winpthread from: ") + dir);
    OS::get_singleton()->open_dynamic_library(dir + "libwinpthread-1.dll", _handle_winpthread);
    // Don't fail if it's not there — it may already be in PATH
#endif

    print_line(String("FFAVStream: loading FFmpeg from: ") + dir);

#if defined(WINDOWS_ENABLED)
    const String avutil_name     = "avutil-56.dll";
    const String swresample_name = "swresample-3.dll";
    const String avcodec_name    = "avcodec-58.dll";
    const String avformat_name   = "avformat-58.dll";
#elif defined(MACOS_ENABLED)
    const String avutil_name     = "libavutil.56.dylib";
    const String swresample_name = "libswresample.3.dylib";
    const String avcodec_name    = "libavcodec.58.dylib";
    const String avformat_name   = "libavformat.58.dylib";
#else
    const String avutil_name     = "libavutil.so.56";
    const String swresample_name = "libswresample.so.3";
    const String avcodec_name    = "libavcodec.so.58";
    const String avformat_name   = "libavformat.so.58";
#endif

    struct { const String &name; void **handle; } libs[] = {
        { avutil_name,     &_handle_avutil     },
        { swresample_name, &_handle_swresample },
        { avcodec_name,    &_handle_avcodec    },
        { avformat_name,   &_handle_avformat   },
    };

    for (auto &lib : libs) {
        Error err = OS::get_singleton()->open_dynamic_library(dir + lib.name, *lib.handle);
        if (err != OK) {
            print_error(String("FFAVStream: failed to load ") + lib.name);
            print_error(String("FFAVStream: expected path: ") + dir + lib.name);
            print_error("FFAVStream: call FFAVStream.set_library_path() with the correct directory before play()");
            unload_libraries();
            return false;
        }
    }

    bool ok = true;
    ok &= _load_symbol(_handle_avformat, "avformat_open_input",       (void*&)g_ff.avformat_open_input);
    ok &= _load_symbol(_handle_avformat, "avformat_find_stream_info", (void*&)g_ff.avformat_find_stream_info);
    ok &= _load_symbol(_handle_avformat, "avformat_close_input",      (void*&)g_ff.avformat_close_input);
    ok &= _load_symbol(_handle_avformat, "av_read_frame",             (void*&)g_ff.av_read_frame);
    ok &= _load_symbol(_handle_avcodec,  "av_packet_alloc",           (void*&)g_ff.av_packet_alloc);
    ok &= _load_symbol(_handle_avcodec,  "av_packet_free",            (void*&)g_ff.av_packet_free);
    ok &= _load_symbol(_handle_avcodec,  "av_packet_unref",           (void*&)g_ff.av_packet_unref);
    ok &= _load_symbol(_handle_avutil,   "av_malloc",                 (void*&)g_ff.av_malloc);
    ok &= _load_symbol(_handle_avutil,   "av_free",                   (void*&)g_ff.av_free);
    ok &= _load_symbol(_handle_avutil,   "av_dict_set",               (void*&)g_ff.av_dict_set);
    ok &= _load_symbol(_handle_avutil,   "av_dict_free",              (void*&)g_ff.av_dict_free);
    ok &= _load_symbol(_handle_avutil,   "av_strerror",               (void*&)g_ff.av_strerror);

    if (!ok) {
        g_ff = FFmpegFunctions();
        unload_libraries();
        return false;
    }

    print_line("FFAVStream: FFmpeg loaded successfully");
    return true;
}

void FFAVStream::unload_libraries() {
    g_ff = FFmpegFunctions();
    if (_handle_avformat)   { OS::get_singleton()->close_dynamic_library(_handle_avformat);   _handle_avformat   = nullptr; }
    if (_handle_avcodec)    { OS::get_singleton()->close_dynamic_library(_handle_avcodec);    _handle_avcodec    = nullptr; }
    if (_handle_swresample) { OS::get_singleton()->close_dynamic_library(_handle_swresample); _handle_swresample = nullptr; }
    if (_handle_avutil)     { OS::get_singleton()->close_dynamic_library(_handle_avutil);     _handle_avutil     = nullptr; }
#if defined(WINDOWS_ENABLED)
    if (_handle_winpthread) { OS::get_singleton()->close_dynamic_library(_handle_winpthread); _handle_winpthread = nullptr; }
#endif
}

bool FFAVStream::libraries_loaded() {
    return g_ff.is_loaded();
}

// ---------------------------------------------------------------------------
// Bind methods
// ---------------------------------------------------------------------------
void FFAVStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("play", "url"),               &FFAVStream::play);
    ClassDB::bind_method(D_METHOD("_emit_stream_opened"),       &FFAVStream::_emit_stream_opened);
    ClassDB::bind_method(D_METHOD("_emit_stream_closed"),       &FFAVStream::_emit_stream_closed);

    // stream_opened: no args
    // stream_closed: int state — check FFAVStream.State enum for value
    ADD_SIGNAL(MethodInfo("stream_opened"));
    ADD_SIGNAL(MethodInfo("stream_closed",
        PropertyInfo(Variant::INT, "state")));
    ClassDB::bind_method(D_METHOD("stop"),                      &FFAVStream::stop);
    ClassDB::bind_method(D_METHOD("get_state"),                 &FFAVStream::get_state);
    ClassDB::bind_method(D_METHOD("load_libraries"),            &FFAVStream::load_libraries_inst);
    ClassDB::bind_method(D_METHOD("unload_libraries"),          &FFAVStream::unload_libraries_inst);
    ClassDB::bind_method(D_METHOD("libraries_loaded"),          &FFAVStream::libraries_loaded_inst);
    ClassDB::bind_method(D_METHOD("set_library_path", "path"),  &FFAVStream::set_library_path_inst);
    ClassDB::bind_method(D_METHOD("get_library_path"),          &FFAVStream::get_library_path_inst);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "library_path"),
                 "set_library_path", "get_library_path");

    BIND_ENUM_CONSTANT(STATE_STOPPED);
    BIND_ENUM_CONSTANT(STATE_CONNECTING);
    BIND_ENUM_CONSTANT(STATE_PLAYING);
    BIND_ENUM_CONSTANT(STATE_ERROR);
    BIND_ENUM_CONSTANT(STATE_MISSING_LIBS);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
FFAVStream::FFAVStream() {}
FFAVStream::~FFAVStream() { stop(); }

// ---------------------------------------------------------------------------
// play() / stop()
// ---------------------------------------------------------------------------
void FFAVStream::_emit_stream_opened() {
    emit_signal("stream_opened");
}

void FFAVStream::_emit_stream_closed() {
    emit_signal("stream_closed", (int)state);
}

void FFAVStream::play(const String &p_url) {
    if (!g_ff.is_loaded()) {
        if (!load_libraries()) {
            state = STATE_MISSING_LIBS;
            return;
        }
    }
    stop();
    url = p_url;
    state = STATE_CONNECTING;
    running = true;
    recv_thread.start(_recv_thread_func, this);
}

void FFAVStream::stop() {
    if (!running) return;
    running = false;
    if (recv_thread.is_started()) recv_thread.wait_to_finish();
    if (fmt_ctx && g_ff.avformat_close_input) {
        g_ff.avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
    for (int i = 0; i < QUEUE_SIZE; i++) {
        if (video_queue[i].data) { if (g_ff.av_free) g_ff.av_free(video_queue[i].data); video_queue[i].data = nullptr; }
        if (audio_queue[i].data) { if (g_ff.av_free) g_ff.av_free(audio_queue[i].data); audio_queue[i].data = nullptr; }
    }
    video_queue_read = video_queue_write = 0;
    audio_queue_read = audio_queue_write = 0;
    video_stream_idx = audio_stream_idx = -1;
    state = STATE_STOPPED;
}

// ---------------------------------------------------------------------------
// Receive thread
// ---------------------------------------------------------------------------
void FFAVStream::_recv_thread_func(void *p_userdata) {
    reinterpret_cast<FFAVStream *>(p_userdata)->_recv_loop();
}

void FFAVStream::_recv_loop() {
    AVDictionary *opts = nullptr;
    g_ff.av_dict_set(&opts, "fflags",          "nobuffer",  0);
    g_ff.av_dict_set(&opts, "flags",           "low_delay", 0);
    g_ff.av_dict_set(&opts, "analyzeduration", "1000000",   0);
    g_ff.av_dict_set(&opts, "probesize",       "1000000",   0);
    g_ff.av_dict_set(&opts, "timeout",         "2000000", 0);
    g_ff.av_dict_set(&opts, "stimeout",        "2000000", 0);

    AVPacket *pkt = nullptr;

    int ret = g_ff.avformat_open_input(&fmt_ctx, url.utf8().get_data(), nullptr, &opts);
    g_ff.av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[128];
        g_ff.av_strerror(ret, errbuf, sizeof(errbuf));
        print_error(String("FFAVStream: failed to open ") + url + " : " + errbuf);
        state = STATE_ERROR;
        goto done;
    }

    if (g_ff.avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        print_error("FFAVStream: failed to find stream info");
        state = STATE_ERROR;
        goto done;
    }

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVMediaType type = fmt_ctx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && video_stream_idx == -1) video_stream_idx = i;
        else if (type == AVMEDIA_TYPE_AUDIO && audio_stream_idx == -1) audio_stream_idx = i;
    }

    if (video_stream_idx == -1) {
        print_error("FFAVStream: no video stream found");
        state = STATE_ERROR;
        goto done;
    }

    print_line(String("FFAVStream: opened — video=") + itos(video_stream_idx) +
               " audio=" + itos(audio_stream_idx));
    state = STATE_PLAYING;
    call_deferred("_emit_stream_opened");

    pkt = g_ff.av_packet_alloc();
    while (running) {
        ret = g_ff.av_read_frame(fmt_ctx, pkt);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            OS::get_singleton()->delay_usec(1000);
            continue;
        }
        if (ret < 0) { print_error("FFAVStream: read error"); state = STATE_ERROR; break; }
        if (pkt->stream_index == video_stream_idx)       _push_video_packet(pkt);
        else if (pkt->stream_index == audio_stream_idx)  _push_audio_packet(pkt);
        g_ff.av_packet_unref(pkt);
    }
    g_ff.av_packet_free(&pkt);

done:
    // Always fires regardless of how we exited — connection failure, read error, or clean stop
    call_deferred("_emit_stream_closed");
}

// ---------------------------------------------------------------------------
// Packet queues
// ---------------------------------------------------------------------------
void FFAVStream::_push_video_packet(AVPacket *pkt) {
    mutex.lock();
    int next = (video_queue_write + 1) % QUEUE_SIZE;
    if (next == video_queue_read) {
        if (video_queue[video_queue_read].data) g_ff.av_free(video_queue[video_queue_read].data);
        video_queue[video_queue_read].data = nullptr;
        video_queue_read = (video_queue_read + 1) % QUEUE_SIZE;
    }
    Packet &s = video_queue[video_queue_write];
    s.data = (uint8_t *)g_ff.av_malloc(pkt->size);
    memcpy(s.data, pkt->data, pkt->size);
    s.size = pkt->size; s.pts = pkt->pts;
    video_queue_write = next;
    mutex.unlock();
}

void FFAVStream::_push_audio_packet(AVPacket *pkt) {
    mutex.lock();
    int next = (audio_queue_write + 1) % QUEUE_SIZE;
    if (next == audio_queue_read) {
        if (audio_queue[audio_queue_read].data) g_ff.av_free(audio_queue[audio_queue_read].data);
        audio_queue[audio_queue_read].data = nullptr;
        audio_queue_read = (audio_queue_read + 1) % QUEUE_SIZE;
    }
    Packet &s = audio_queue[audio_queue_write];
    s.data = (uint8_t *)g_ff.av_malloc(pkt->size);
    memcpy(s.data, pkt->data, pkt->size);
    s.size = pkt->size; s.pts = pkt->pts;
    audio_queue_write = next;
    mutex.unlock();
}

bool FFAVStream::pop_video_packet(uint8_t **out_data, int *out_size, int64_t *out_pts) {
    mutex.lock();
    if (video_queue_read == video_queue_write) { mutex.unlock(); return false; }
    Packet &s = video_queue[video_queue_read];
    *out_data = s.data; *out_size = s.size; *out_pts = s.pts;
    s.data = nullptr;
    video_queue_read = (video_queue_read + 1) % QUEUE_SIZE;
    mutex.unlock();
    return true;
}

bool FFAVStream::pop_audio_packet(uint8_t **out_data, int *out_size, int64_t *out_pts) {
    mutex.lock();
    if (audio_queue_read == audio_queue_write) { mutex.unlock(); return false; }
    Packet &s = audio_queue[audio_queue_read];
    *out_data = s.data; *out_size = s.size; *out_pts = s.pts;
    s.data = nullptr;
    audio_queue_read = (audio_queue_read + 1) % QUEUE_SIZE;
    mutex.unlock();
    return true;
}

void FFAVStream::free_packet_data(uint8_t *data) {
    if (g_ff.av_free) g_ff.av_free(data);
}
