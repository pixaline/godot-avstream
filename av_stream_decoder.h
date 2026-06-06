#ifndef AV_STREAM_DECODER_H
#define AV_STREAM_DECODER_H

#include "core/object.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/reference.h"
#include "scene/resources/texture.h"
#include "scene/resources/audio_stream_sample.h"
#include "servers/audio/audio_stream.h"
#include "av_stream.h"

// vpx and opus linked statically — include headers directly, no function pointers needed
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include <opus/opus.h>

// ---------------------------------------------------------------------------
// FFAVStreamDecoder
//
// Sits on top of FFAVStream. Call update() every frame from _process().
// Decodes VP8 → ImageTexture, Opus → pushes PCM into AudioStreamGenerator.
// ---------------------------------------------------------------------------
class FFAVStreamDecoder : public Object {
    GDCLASS(FFAVStreamDecoder, Object);

public:
    enum State {
        STATE_STOPPED,
        STATE_RUNNING,
        STATE_ERROR,
    };

private:
    FFAVStream *_stream = nullptr;

    // libvpx state
    vpx_codec_ctx_t _vpx_ctx;
    bool            _vpx_initialized = false;

    // libopus state
    OpusDecoder    *_opus_ctx = nullptr;
    static const int OPUS_SAMPLE_RATE = 48000;
    static const int OPUS_CHANNELS    = 2;
    static const int OPUS_MAX_FRAME   = 5760;

    Ref<ImageTexture> _texture;
    int _video_width  = 0;
    int _video_height = 0;

    State _state = STATE_STOPPED;

    bool _init_vpx();
    bool _init_opus();
    void _decode_video(uint8_t *data, int size);
    void _decode_audio(uint8_t *data, int size);
    void _yuv420_to_rgba(vpx_image_t *img, PoolVector<uint8_t> &out);

protected:
    static void _bind_methods();

public:
    void set_stream(Object *p_stream);
    void update();
    void start();
    void stop();

    State        get_state()   const { return _state; }
    Ref<Texture> get_texture() const { return _texture; }

    FFAVStreamDecoder();
    ~FFAVStreamDecoder();
};

VARIANT_ENUM_CAST(FFAVStreamDecoder::State);

#endif
