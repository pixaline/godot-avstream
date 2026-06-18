#include "av_stream_decoder.h"
#include "core/print_string.h"
#include "core/os/os.h"
#include "core/project_settings.h"
#include "core/image.h"

// ---------------------------------------------------------------------------
// Bind methods
// ---------------------------------------------------------------------------
void FFAVStreamDecoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_stream", "stream"), &FFAVStreamDecoder::set_stream);
    ClassDB::bind_method(D_METHOD("update"),               &FFAVStreamDecoder::update);
    ClassDB::bind_method(D_METHOD("start"),                &FFAVStreamDecoder::start);
    ClassDB::bind_method(D_METHOD("stop"),                 &FFAVStreamDecoder::stop);
    ClassDB::bind_method(D_METHOD("get_state"),            &FFAVStreamDecoder::get_state);
    ClassDB::bind_method(D_METHOD("get_texture"),          &FFAVStreamDecoder::get_texture);
    ClassDB::bind_method(D_METHOD("set_video_codec_id", "id"), &FFAVStreamDecoder::set_video_codec_id);

    ADD_SIGNAL(MethodInfo("audio_frame",
        PropertyInfo(Variant::POOL_VECTOR2_ARRAY, "frames")));

    BIND_ENUM_CONSTANT(STATE_STOPPED);
    BIND_ENUM_CONSTANT(STATE_RUNNING);
    BIND_ENUM_CONSTANT(STATE_ERROR);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
FFAVStreamDecoder::FFAVStreamDecoder() {
    memset(&_vpx_ctx, 0, sizeof(_vpx_ctx));
}

FFAVStreamDecoder::~FFAVStreamDecoder() {
    stop();
}

// ---------------------------------------------------------------------------
// set_stream / start / stop
// ---------------------------------------------------------------------------
void FFAVStreamDecoder::set_stream(Object *p_stream) {
    _stream = Object::cast_to<FFAVStream>(p_stream);
}

void FFAVStreamDecoder::start() {
    if (!_stream) {
        print_error("FFAVStreamDecoder: no stream set, call set_stream() first");
        _state = STATE_ERROR;
        return;
    }

    _video_codec_id = _stream->get_video_codec_id();

    if (!_init_vpx() || !_init_opus()) {
        _state = STATE_ERROR;
        return;
    }

    if (_texture.is_null()) {
        _texture.instance();
    }
    _state = STATE_RUNNING;
}

void FFAVStreamDecoder::stop() {
    _state = STATE_STOPPED;

    if (_vpx_initialized) {
        vpx_codec_destroy(&_vpx_ctx);
        _vpx_initialized = false;
    }

    if (_opus_ctx) {
        opus_decoder_destroy(_opus_ctx);
        _opus_ctx = nullptr;
    }

    // Keep _texture alive so GDScript references remain valid across reconnects
    _video_width = _video_height = 0;
}

// ---------------------------------------------------------------------------
// Codec initialisation
// ---------------------------------------------------------------------------
bool FFAVStreamDecoder::_init_vpx() {
    if (_vpx_initialized) {
        vpx_codec_destroy(&_vpx_ctx);
        _vpx_initialized = false;
    }
    vpx_codec_iface_t *iface = (_video_codec_id == AV_CODEC_ID_VP9) ?
        vpx_codec_vp9_dx() : vpx_codec_vp8_dx();
    vpx_codec_dec_cfg_t cfg = {};
    cfg.threads = 2;

    vpx_codec_err_t err = vpx_codec_dec_init_ver(
        &_vpx_ctx, iface, &cfg, 0, VPX_DECODER_ABI_VERSION);

    if (err != VPX_CODEC_OK) {
        print_error("FFAVStreamDecoder: failed to init VPx decoder");
        return false;
    }
    _vpx_initialized = true;
    return true;
}

bool FFAVStreamDecoder::_init_opus() {
    if (_opus_ctx) {
        opus_decoder_destroy(_opus_ctx);
        _opus_ctx = nullptr;
    }
    int err = 0;
    _opus_ctx = opus_decoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS, &err);
    if (err != OPUS_OK || !_opus_ctx) {
        print_error("FFAVStreamDecoder: failed to init Opus decoder");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// update() — call every frame from GDScript _process()
// ---------------------------------------------------------------------------
void FFAVStreamDecoder::update() {
    if (_state != STATE_RUNNING || !_stream) return;

    uint8_t *data = nullptr;
    int size      = 0;
    int64_t pts   = 0;

    while (_stream->pop_video_packet(&data, &size, &pts)) {
        _decode_video(data, size);
        _stream->free_packet_data(data);
    }

    while (_stream->pop_audio_packet(&data, &size, &pts)) {
        _decode_audio(data, size);
        _stream->free_packet_data(data);
    }
}

// ---------------------------------------------------------------------------
// Video decode: VP8 packet → YUV → RGBA → ImageTexture
// ---------------------------------------------------------------------------
void FFAVStreamDecoder::_decode_video(uint8_t *data, int size) {
    vpx_codec_err_t err = vpx_codec_decode(&_vpx_ctx, data, size, nullptr, 0);
    if (err != VPX_CODEC_OK) return;

    vpx_codec_iter_t iter = nullptr;
    vpx_image_t *img = nullptr;

    while ((img = vpx_codec_get_frame(&_vpx_ctx, &iter))) {
        if (img->fmt != VPX_IMG_FMT_I420) continue;

        PoolVector<uint8_t> rgba;
        _yuv420_to_rgba(img, rgba);

        Ref<Image> frame;
        frame.instance();
        frame->create(img->d_w, img->d_h, false, Image::FORMAT_RGBA8, rgba);

        if (_video_width != (int)img->d_w || _video_height != (int)img->d_h) {
            _video_width  = img->d_w;
            _video_height = img->d_h;
            _texture->create_from_image(frame, 0);
        } else {
            _texture->set_data(frame);
        }
    }
}

// ---------------------------------------------------------------------------
// YUV420 → RGBA conversion
// ---------------------------------------------------------------------------
void FFAVStreamDecoder::_yuv420_to_rgba(vpx_image_t *img, PoolVector<uint8_t> &out) {
    const int w = img->d_w;
    const int h = img->d_h;
    out.resize(w * h * 4);

    PoolVector<uint8_t>::Write wr = out.write();
    uint8_t *dst = wr.ptr();

    const uint8_t *y_plane = img->planes[VPX_PLANE_Y];
    const uint8_t *u_plane = img->planes[VPX_PLANE_U];
    const uint8_t *v_plane = img->planes[VPX_PLANE_V];

    const int y_stride = img->stride[VPX_PLANE_Y];
    const int u_stride = img->stride[VPX_PLANE_U];
    const int v_stride = img->stride[VPX_PLANE_V];

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int y = y_plane[row * y_stride + col];
            int u = u_plane[(row / 2) * u_stride + (col / 2)] - 128;
            int v = v_plane[(row / 2) * v_stride + (col / 2)] - 128;

            int r = CLAMP(y + (int)(1.402f  * v),                    0, 255);
            int g = CLAMP(y - (int)(0.344f  * u + 0.714f * v),       0, 255);
            int b = CLAMP(y + (int)(1.772f  * u),                    0, 255);

            int idx = (row * w + col) * 4;
            dst[idx + 0] = (uint8_t)r;
            dst[idx + 1] = (uint8_t)g;
            dst[idx + 2] = (uint8_t)b;
            dst[idx + 3] = 255;
        }
    }
}

// ---------------------------------------------------------------------------
// Audio decode: Opus packet → PCM int16 → PoolVector2Array → signal
// ---------------------------------------------------------------------------
void FFAVStreamDecoder::_decode_audio(uint8_t *data, int size) {
    static opus_int16 pcm[5760 * 2];

    int samples = opus_decode(_opus_ctx, data, size, pcm, OPUS_MAX_FRAME, 0);
    if (samples <= 0) return;

    PoolVector2Array frames;
    frames.resize(samples);
    PoolVector2Array::Write wr = frames.write();

    for (int i = 0; i < samples; i++) {
        wr[i] = Vector2(
            pcm[i * 2]     / 32768.0f,
            pcm[i * 2 + 1] / 32768.0f
        );
    }

    emit_signal("audio_frame", frames);
}
