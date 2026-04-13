#include "dji_encoder.h"
#include "decoder_impl.h"

#include <new>

extern "C" {

DJI_API DJIDecoderHandle DJI_Decoder_Create(void) {
    try {
        DecoderImpl* impl = new (std::nothrow) DecoderImpl();
        return reinterpret_cast<DJIDecoderHandle>(impl);
    } catch (...) {
        return nullptr;
    }
}

DJI_API int DJI_Decoder_Init(DJIDecoderHandle handle) {
    if (!handle) {
        return DJI_ERROR_INVALID_PARAM;
    }

    DecoderImpl* impl = reinterpret_cast<DecoderImpl*>(handle);

    bool ok = impl->Initialize();
    if (!ok) {
        return DJI_ERROR_INIT_FAILED;
    }

    return DJI_OK;
}

DJI_API int DJI_Decoder_Decode(DJIDecoderHandle handle,
                                const uint8_t* h264_data,
                                int h264_size,
                                DJIDecoderOutput* output) {
    if (!handle || !h264_data || h264_size <= 0 || !output) {
        return DJI_ERROR_INVALID_PARAM;
    }

    DecoderImpl* impl = reinterpret_cast<DecoderImpl*>(handle);

    if (!impl->IsInitialized()) {
        return DJI_ERROR_NOT_INITIALIZED;
    }

    bool ok = impl->DecodeFrame(h264_data, h264_size);
    if (!ok) {
        // 可能需要更多数据（正常情况），返回特定值
        output->r_plane = nullptr;
        output->g_plane = nullptr;
        output->b_plane = nullptr;
        output->width = 0;
        output->height = 0;
        return DJI_DECODER_NEED_MORE_DATA;
    }

    // 填充输出
    output->r_plane = impl->GetOutputR();
    output->g_plane = impl->GetOutputG();
    output->b_plane = impl->GetOutputB();
    output->width = impl->GetOutputWidth();
    output->height = impl->GetOutputHeight();

    return DJI_OK;
}

DJI_API int DJI_Decoder_Flush(DJIDecoderHandle handle, DJIDecoderOutput* output) {
    if (!handle || !output) {
        return DJI_ERROR_INVALID_PARAM;
    }

    DecoderImpl* impl = reinterpret_cast<DecoderImpl*>(handle);

    if (!impl->IsInitialized()) {
        return DJI_ERROR_NOT_INITIALIZED;
    }

    bool ok = impl->Flush();
    if (ok) {
        output->r_plane = impl->GetOutputR();
        output->g_plane = impl->GetOutputG();
        output->b_plane = impl->GetOutputB();
        output->width = impl->GetOutputWidth();
        output->height = impl->GetOutputHeight();
        return 1;  // 有数据
    }

    return 0;  // 没有更多数据
}

DJI_API void DJI_Decoder_Destroy(DJIDecoderHandle handle) {
    if (handle) {
        DecoderImpl* impl = reinterpret_cast<DecoderImpl*>(handle);
        delete impl;
    }
}

} // extern "C"
