#ifndef AV_STREAM_H
#define AV_STREAM_H

#include "core/object.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include "core/os/os.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

// ---------------------------------------------------------------------------
// Function pointer table for dynamically loaded FFmpeg symbols
// ---------------------------------------------------------------------------
struct FFmpegFunctions {
    int         (*avformat_open_input)(AVFormatContext**, const char*, AVInputFormat*, AVDictionary**) = nullptr;
    int         (*avformat_find_stream_info)(AVFormatContext*, AVDictionary**) = nullptr;
    void        (*avformat_close_input)(AVFormatContext**) = nullptr;
    int         (*av_read_frame)(AVFormatContext*, AVPacket*) = nullptr;
    AVPacket*   (*av_packet_alloc)() = nullptr;
    void        (*av_packet_free)(AVPacket**) = nullptr;
    void        (*av_packet_unref)(AVPacket*) = nullptr;
    void*       (*av_malloc)(size_t) = nullptr;
    void        (*av_free)(void*) = nullptr;
    int         (*av_dict_set)(AVDictionary**, const char*, const char*, int) = nullptr;
    void        (*av_dict_free)(AVDictionary**) = nullptr;
    int         (*av_strerror)(int, char*, size_t) = nullptr;

    bool is_loaded() const { return avformat_open_input != nullptr; }
};

extern FFmpegFunctions g_ff;

// ---------------------------------------------------------------------------
// FFAVStream
// ---------------------------------------------------------------------------
class FFAVStream : public Object {
    GDCLASS(FFAVStream, Object);

public:
    enum State {
        STATE_STOPPED,
        STATE_CONNECTING,
        STATE_PLAYING,
        STATE_ERROR,
        STATE_MISSING_LIBS,
    };

    // Set the directory containing the FFmpeg shared libraries.
    // Accepts any Godot path: "user://lib/ffmpeg", "res://lib/ffmpeg",
    // or an absolute OS path. Must be set before calling load_libraries()
    // or play(). Stored statically so you only need to set it once.
    static void        set_library_path(const String &p_path);
    static String      get_library_path();

    // Explicitly load/unload. play() calls load_libraries() lazily if needed.
    static bool        load_libraries();
    static void        unload_libraries();
    static bool        libraries_loaded();

private:
    static String  _library_path;
    static void   *_handle_avutil;
    static void   *_handle_avcodec;
    static void   *_handle_avformat;
    static void   *_handle_swresample;

    AVFormatContext *fmt_ctx = nullptr;
    int video_stream_idx = -1;
    int audio_stream_idx = -1;

    static void *_handle_winpthread;
    Thread recv_thread;
    Mutex mutex;
    volatile bool running = false;

    struct Packet {
        uint8_t *data = nullptr;
        int size = 0;
        int64_t pts = AV_NOPTS_VALUE;
    };

    static const int QUEUE_SIZE = 64;
    Packet video_queue[QUEUE_SIZE];
    Packet audio_queue[QUEUE_SIZE];
    int video_queue_write = 0;
    int video_queue_read  = 0;
    int audio_queue_write = 0;
    int audio_queue_read  = 0;

    State state = STATE_STOPPED;
    String url;

    static void _recv_thread_func(void *p_userdata);
    void _recv_loop();
    void _push_video_packet(AVPacket *pkt);
    void _push_audio_packet(AVPacket *pkt);

    static bool   _load_symbol(void *handle, const char *name, void *&out_ptr);
    static String _resolve_path(const String &p_path);

protected:
    static void _bind_methods();

public:
    void play(const String &p_url);
    void stop();
    State get_state() const { return state; }

    // Instance trampolines — ClassDB cannot bind static methods directly
    void   set_library_path_inst(const String &p_path) { set_library_path(p_path); }
    String get_library_path_inst() const               { return get_library_path(); }
    bool   load_libraries_inst()                       { return load_libraries(); }
    void   unload_libraries_inst()                     { unload_libraries(); }
    bool   libraries_loaded_inst() const               { return libraries_loaded(); }

    bool pop_video_packet(uint8_t **out_data, int *out_size, int64_t *out_pts);
    bool pop_audio_packet(uint8_t **out_data, int *out_size, int64_t *out_pts);
    void free_packet_data(uint8_t *data);

    // Emitted on main thread via call_deferred
    // stream_opened: fires when avformat connects and finds streams
    // stream_closed: fires when the recv loop exits for any reason, carries the State
    void _emit_stream_opened();   // called via deferred
    void _emit_stream_closed();   // called via deferred

    FFAVStream();
    ~FFAVStream();
};

VARIANT_ENUM_CAST(FFAVStream::State);

#endif
