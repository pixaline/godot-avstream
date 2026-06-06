#include "register_types.h"
#include "core/class_db.h"
#include "av_stream.h"
#include "av_stream_decoder.h"

void register_av_stream_types() {
    ClassDB::register_class<FFAVStream>();
    ClassDB::register_class<FFAVStreamDecoder>();
}

void unregister_av_stream_types() {
    FFAVStream::unload_libraries();
    // FFAVStreamDecoder has no dynamic libs to unload — vpx/opus are static
}
