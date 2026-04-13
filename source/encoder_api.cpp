#include "dji_encoder.h"
#include "encoder_impl.h"

#include <cstring>
#include <new>

extern "C" {

DJI_API DJIEncoderHandle DJI_Encoder_Create(void) {
    try {
        EncoderImpl* impl = new (std::nothrow) EncoderImpl();
        return reinterpret_cast<DJIEncoderHandle>(impl);
    } catch (...) {
        return nullptr;
    }
}

DJI_API int DJI_Encoder_Init(DJIEncoderHandle handle, const DJIEncoderConfig* config) {
    if (!handle) {
        return DJI_ERROR_INVALID_PARAM;
    }

    // 默认配置
    int fps = 30;
    int bitrate_kbps = 6000;
    // int gop_size = 30;  // 内部使用，不在 config 中存储

    if (config) {
        if (config->fps > 0) {
            fps = config->fps;
        }
        if (config->bitrate_kbps > 0) {
            bitrate_kbps = config->bitrate_kbps;
        }
    }

    // 固定使用 1280x720
    const int width = 1280;
    const int height = 720;

    EncoderImpl* impl = reinterpret_cast<EncoderImpl*>(handle);
    
    bool ok = impl->Initialize(width, height, fps, bitrate_kbps);
    if (!ok) {
        return DJI_ERROR_INIT_FAILED;
    }

    return DJI_OK;
}

DJI_API int DJI_Encoder_Encode(DJIEncoderHandle handle, 
                                const DJIEncoderInput* input, 
                                DJIEncoderOutput* output) {
    if (!handle || !input || !output) {
        return DJI_ERROR_INVALID_PARAM;
    }

    if (!input->r_plane || !input->g_plane || !input->b_plane) {
        return DJI_ERROR_INVALID_PARAM;
    }

    EncoderImpl* impl = reinterpret_cast<EncoderImpl*>(handle);

    if (!impl->IsInitialized()) {
        return DJI_ERROR_NOT_INITIALIZED;
    }

    bool ok = impl->EncodeFrame(input->r_plane, input->g_plane, input->b_plane);
    if (!ok) {
        // 可能是正常的 MORE_DATA 情况，或者编码失败
        // 这里简化处理，如果没有输出数据就认为需要更多输入
        if (impl->GetLastOutputSize() == 0) {
            return DJI_ERROR_ENCODE_FAILED;
        }
    }

    // 填充输出结构体
    output->data = impl->GetLastOutputData();
    output->size = impl->GetLastOutputSize();
    output->is_keyframe = impl->IsLastKeyframe() ? 1 : 0;
    output->pts = impl->GetLastPTS();

    return DJI_OK;
}

DJI_API int DJI_Encoder_Flush(DJIEncoderHandle handle, DJIEncoderOutput* output) {
    if (!handle || !output) {
        return DJI_ERROR_INVALID_PARAM;
    }

    EncoderImpl* impl = reinterpret_cast<EncoderImpl*>(handle);

    if (!impl->IsInitialized()) {
        return DJI_ERROR_NOT_INITIALIZED;
    }

    bool ok = impl->Flush();
    if (ok && impl->GetLastOutputSize() > 0) {
        output->data = impl->GetLastOutputData();
        output->size = impl->GetLastOutputSize();
        output->is_keyframe = impl->IsLastKeyframe() ? 1 : 0;
        output->pts = impl->GetLastPTS();
        return 1;
    }

    return 0;  // 没有更多数据
}

DJI_API void DJI_Encoder_Destroy(DJIEncoderHandle handle) {
    if (handle) {
        EncoderImpl* impl = reinterpret_cast<EncoderImpl*>(handle);
        delete impl;
    }
}

DJI_API const char* DJI_Encoder_GetVersion(void) {
    return "DjiH264Encoder v1.0.0 (Intel oneVPL)";
}

DJI_API const char* DJI_Encoder_GetErrorString(int error_code) {
    switch (error_code) {
        case DJI_OK:
            return "Success";
        case DJI_ERROR_INVALID_PARAM:
            return "Invalid parameter (null pointer or invalid value)";
        case DJI_ERROR_INIT_FAILED:
            return "Initialization failed (check Intel GPU driver)";
        case DJI_ERROR_ENCODE_FAILED:
            return "Encode failed";
        case DJI_ERROR_NOT_INITIALIZED:
            return "Encoder not initialized";
        case DJI_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        default:
            return "Unknown error";
    }
}

} // extern "C"
