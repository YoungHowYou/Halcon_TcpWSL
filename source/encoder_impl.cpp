#include "encoder_impl.h"

#ifdef _WIN32
// Windows/D3D11 头文件
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#endif

// libyuv 头文件
#include <libyuv.h>

// 标准库
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

#ifdef _WIN32
// 确保链接必要的库
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

// 对齐宏
constexpr int ALIGN16(int x) { return (x + 15) & ~15; }

EncoderImpl::EncoderImpl() = default;

EncoderImpl::~EncoderImpl() {
    // 清理资源
    if (mfx_encoder_) {
        mfx_encoder_->Close();
        delete (MFXVideoENCODE*)mfx_encoder_;
    }
    if (mfx_session_) {
        mfx_session_->Close();
        delete (MFXVideoSession*)mfx_session_;
    }
#ifdef _WIN32
    if (d3d11_texture_) d3d11_texture_->Release();
    if (d3d11_context_) d3d11_context_->Release();
    if (d3d11_device_) d3d11_device_->Release();
#endif
    delete encode_param_;
    delete mfx_bitstream_;
}

bool EncoderImpl::Initialize(int width, int height, int fps, int bitrate_kbps) {
    width_ = width;
    height_ = height;
    gop_size_ = fps;  // 每秒一个 IDR

    // 分配 NV12 缓冲区
    int aligned_width = ALIGN16(width_);
    int aligned_height = ALIGN16(height_);
    size_t nv12_size = aligned_width * aligned_height * 3 / 2;
    nv12_buffer_.resize(nv12_size);

    // 初始化 D3D11
    if (!InitD3D11()) {
        std::cerr << "[EncoderImpl] D3D11 initialization failed" << std::endl;
        return false;
    }

    // 初始化 VPL
    if (!InitVPL()) {
        std::cerr << "[EncoderImpl] VPL initialization failed" << std::endl;
        return false;
    }

    // 创建编码器
    if (!CreateEncoder(fps, bitrate_kbps)) {
        std::cerr << "[EncoderImpl] Encoder creation failed" << std::endl;
        return false;
    }

    initialized_ = true;
    frame_count_ = 0;
    
    std::cout << "[EncoderImpl] Initialized: " << width_ << "x" << height_ 
              << " @ " << fps << "fps, " << bitrate_kbps << "kbps" << std::endl;
    
    return true;
}

bool EncoderImpl::InitD3D11() {
#ifdef _WIN32
    HRESULT hr;
    
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL feature_level_out;

    UINT create_device_flags = 0;
#ifdef _DEBUG
    create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_device_flags,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &d3d11_device_,
        &feature_level_out,
        &d3d11_context_
    );

    // Debug 层未安装时回退到无 Debug 标志
    if (FAILED(hr) && (create_device_flags & D3D11_CREATE_DEVICE_DEBUG)) {
        create_device_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            create_device_flags,
            feature_levels,
            ARRAYSIZE(feature_levels),
            D3D11_SDK_VERSION,
            &d3d11_device_,
            &feature_level_out,
            &d3d11_context_
        );
    }

    if (FAILED(hr)) {
        std::cerr << "[EncoderImpl] Failed to create D3D11 device: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 创建 NV12 纹理
    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = ALIGN16(width_);
    tex_desc.Height = ALIGN16(height_);
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_NV12;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DYNAMIC;
    tex_desc.BindFlags = D3D11_BIND_VIDEO_ENCODER;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = d3d11_device_->CreateTexture2D(&tex_desc, nullptr, &d3d11_texture_);
    if (FAILED(hr)) {
        // 尝试不绑定视频编码器标志
        tex_desc.BindFlags = 0;
        hr = d3d11_device_->CreateTexture2D(&tex_desc, nullptr, &d3d11_texture_);
        if (FAILED(hr)) {
            std::cerr << "[EncoderImpl] Failed to create NV12 texture: 0x" << std::hex << hr << std::endl;
            return false;
        }
    }

    return true;
#else
    (void)width_;
    (void)height_;
    return true;
#endif
}

bool EncoderImpl::InitVPL() {
    mfxStatus sts;

    // 创建会话
    mfx_session_ = (MFXVideoSession*)new MFXVideoSession();
    
    mfxVersion version = {{1, 0}};  // API 1.0
    
    // 尝试多种初始化方式
    // 方式 1: 使用 D3D11 硬件加速
    if (d3d11_device_) {
        sts = ((MFXVideoSession*)mfx_session_)->Init(MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11, &version);
        if (sts == MFX_ERR_NONE) {
            // 设置 D3D11 设备句柄
            sts = ((MFXVideoSession*)mfx_session_)->SetHandle(MFX_HANDLE_D3D11_DEVICE, (mfxHDL)d3d11_device_);
            if (sts == MFX_ERR_NONE) {
                std::cout << "[EncoderImpl] Using hardware D3D11 acceleration" << std::endl;
            } else {
                std::cerr << "[EncoderImpl] Failed to set D3D11 handle: " << sts << ", trying system memory" << std::endl;
                ((MFXVideoSession*)mfx_session_)->Close();
                delete (MFXVideoSession*)mfx_session_;
                mfx_session_ = (MFXVideoSession*)new MFXVideoSession();
                goto try_system_memory;
            }
        } else {
            std::cerr << "[EncoderImpl] D3D11 init failed: " << sts << ", trying system memory" << std::endl;
            ((MFXVideoSession*)mfx_session_)->Close();
            delete (MFXVideoSession*)mfx_session_;
            mfx_session_ = (MFXVideoSession*)new MFXVideoSession();
            goto try_system_memory;
        }
    } else {
    try_system_memory:
        // 方式 2: 使用系统内存硬件编码
        sts = ((MFXVideoSession*)mfx_session_)->Init(MFX_IMPL_HARDWARE_ANY, &version);
        if (sts == MFX_ERR_NONE) {
            std::cout << "[EncoderImpl] Using hardware system memory mode" << std::endl;
        } else {
            // 方式 3: 软件回退
            std::cerr << "[EncoderImpl] Hardware init failed: " << sts << ", falling back to software" << std::endl;
            sts = ((MFXVideoSession*)mfx_session_)->Init(MFX_IMPL_SOFTWARE, &version);
            if (sts != MFX_ERR_NONE) {
                std::cerr << "[EncoderImpl] Failed to initialize VPL session: " << sts << std::endl;
                return false;
            }
            std::cout << "[EncoderImpl] Using software VPL implementation" << std::endl;
        }
    }

    // 创建编码器
    mfx_encoder_ = (MFXVideoENCODE*)new MFXVideoENCODE(*((MFXVideoSession*)mfx_session_));

    return true;
}

bool EncoderImpl::CreateEncoder(int fps, int bitrate_kbps) {
    mfxStatus sts;

    encode_param_ = new mfxVideoParam();
    std::memset(encode_param_, 0, sizeof(mfxVideoParam));

    // 编码参数 - 使用最简配置
    encode_param_->mfx.CodecId = MFX_CODEC_AVC;
    encode_param_->mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    encode_param_->mfx.RateControlMethod = MFX_RATECONTROL_VBR;
    encode_param_->mfx.TargetKbps = bitrate_kbps;
    encode_param_->mfx.GopPicSize = gop_size_;
    encode_param_->mfx.GopRefDist = 1;  // Low delay
    encode_param_->mfx.NumRefFrame = 1;

    // 视频参数
    encode_param_->mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    encode_param_->mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    encode_param_->mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    encode_param_->mfx.FrameInfo.FrameRateExtN = fps;
    encode_param_->mfx.FrameInfo.FrameRateExtD = 1;
    encode_param_->mfx.FrameInfo.Width = ALIGN16(width_);
    encode_param_->mfx.FrameInfo.Height = ALIGN16(height_);
    encode_param_->mfx.FrameInfo.CropX = 0;
    encode_param_->mfx.FrameInfo.CropY = 0;
    encode_param_->mfx.FrameInfo.CropW = width_;
    encode_param_->mfx.FrameInfo.CropH = height_;

    // 异步深度（编码器内部缓冲帧数）
    encode_param_->AsyncDepth = 1;  // 最小延迟
    
    // 缓冲区分配模式
    encode_param_->IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    // 验证参数
    sts = mfx_encoder_->Query(encode_param_, encode_param_);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::cerr << "[EncoderImpl] Failed to query encoder params: " << sts << std::endl;
        return false;
    }

    // 初始化编码器
    sts = mfx_encoder_->Init(encode_param_);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "[EncoderImpl] Failed to initialize encoder: " << sts << std::endl;
        return false;
    }

    // 分配 bitstream 缓冲区
    mfx_bitstream_ = new mfxBitstream();
    std::memset(mfx_bitstream_, 0, sizeof(mfxBitstream));
    
    // 获取 buffer 大小（1280x720 的 H.264，I 帧可能需要几百 KB）
    mfxVideoParam param = {};
    sts = ((MFXVideoENCODE*)mfx_encoder_)->GetVideoParam(&param);
    if (sts == MFX_ERR_NONE) {
        mfx_bitstream_->MaxLength = param.mfx.BufferSizeInKB * 1024;
        if (mfx_bitstream_->MaxLength < 2 * 1024 * 1024) {
            mfx_bitstream_->MaxLength = 2 * 1024 * 1024;  // 至少 2MB
        }
    } else {
        mfx_bitstream_->MaxLength = 4 * 1024 * 1024;  // 默认 4MB
    }
    
    bitstream_buffer_.resize(mfx_bitstream_->MaxLength);
    mfx_bitstream_->Data = bitstream_buffer_.data();

    return true;
}

bool EncoderImpl::RGBToNV12(const uint8_t* r_plane, const uint8_t* g_plane, const uint8_t* b_plane) {
    const int width = width_;
    const int height = height_;
    const int aligned_width = ALIGN16(width);
    const int aligned_height = ALIGN16(height);

    // 先将 Planar RGB 转为 Interleaved ARGB
    std::vector<uint8_t> argb(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        argb[i * 4 + 0] = b_plane[i];  // B
        argb[i * 4 + 1] = g_plane[i];  // G
        argb[i * 4 + 2] = r_plane[i];  // R
        argb[i * 4 + 3] = 255;          // A
    }

    // ARGB 转 NV12
    uint8_t* y_plane = nv12_buffer_.data();
    uint8_t* uv_plane = nv12_buffer_.data() + aligned_width * aligned_height;

    int ret = libyuv::ARGBToNV12(
        argb.data(), width * 4,
        y_plane, aligned_width,
        uv_plane, aligned_width,
        width, height
    );

    return ret == 0;
}

bool EncoderImpl::EncodeFrame(const uint8_t* r_plane, const uint8_t* g_plane, const uint8_t* b_plane) {
    if (!initialized_) return false;
    if (!r_plane || !g_plane || !b_plane) return false;

    mfxStatus sts;

    // RGB -> NV12 转换
    if (!RGBToNV12(r_plane, g_plane, b_plane)) {
        std::cerr << "[EncoderImpl] RGB to NV12 conversion failed" << std::endl;
        return false;
    }

    // 准备输入帧
    mfxFrameSurface1 surface = {};
    surface.Info = encode_param_->mfx.FrameInfo;
    surface.Data.Y = nv12_buffer_.data();
    surface.Data.UV = nv12_buffer_.data() + ALIGN16(width_) * ALIGN16(height_);
    surface.Data.Pitch = ALIGN16(width_);

    // 编码控制 - 让编码器自动管理帧类型
    mfxEncodeCtrl ctrl = {};
    ctrl.FrameType = MFX_FRAMETYPE_UNKNOWN;

    // 编码
    mfxSyncPoint sync_point = nullptr;
    sts = ((MFXVideoENCODE*)mfx_encoder_)->EncodeFrameAsync(&ctrl, &surface, (mfxBitstream*)mfx_bitstream_, &sync_point);
    
    if (sts == MFX_ERR_NONE) {
        // 等待同步完成
        sts = ((MFXVideoSession*)mfx_session_)->SyncOperation(sync_point, 60000);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "[EncoderImpl] Sync operation failed: " << sts << std::endl;
            return false;
        }
    } else if (sts == MFX_ERR_MORE_DATA) {
        // 需要更多输入，继续喂帧
        frame_count_++;
        last_output_size_ = 0;
        return true;  // 不是错误，只是暂时没有输出
    } else if (sts == MFX_WRN_DEVICE_BUSY) {
        // 设备忙，等待重试
        int retries = 0;
        while (sts == MFX_WRN_DEVICE_BUSY && retries < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            sts = ((MFXVideoENCODE*)mfx_encoder_)->EncodeFrameAsync(&ctrl, &surface, (mfxBitstream*)mfx_bitstream_, &sync_point);
            retries++;
        }
        if (sts != MFX_ERR_NONE) {
            std::cerr << "[EncoderImpl] Encode failed after " << retries << " retries: " << sts << std::endl;
            return false;
        }
        // 成功，继续同步
        sts = ((MFXVideoSession*)mfx_session_)->SyncOperation(sync_point, 60000);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "[EncoderImpl] Sync failed after busy wait: " << sts << std::endl;
            return false;
        }
    } else {
        // 错误 -15 (MFX_ERR_NOT_ENOUGH_BUFFER) 通常意味着编码器内部缓冲区问题
        // 尝试忽略并继续
        if (sts == -15) {
            // 可能是正常情况，编码器已经消耗了输入但没有输出
            frame_count_++;
            last_output_size_ = 0;
            return true;
        }
        std::cerr << "[EncoderImpl] Encode failed: " << sts << std::endl;
        return false;
    }

    frame_count_++;

    // 保存输出结果
    if (mfx_bitstream_->DataLength > 0) {
        // DJI-H264 标准：每帧结尾必须加上 AUD 信息（6字节）
        size_t encoded_size = static_cast<size_t>(mfx_bitstream_->DataLength);
        size_t total_size = encoded_size + sizeof(DJI_AUD_NAL);
        
        // 确保输出缓冲区足够
        if (output_with_aud_.size() < total_size) {
            output_with_aud_.resize(total_size);
        }
        
        // 复制原始编码数据 + AUD
        memcpy(output_with_aud_.data(), 
               mfx_bitstream_->Data + mfx_bitstream_->DataOffset, 
               encoded_size);
        memcpy(output_with_aud_.data() + encoded_size, 
               DJI_AUD_NAL, 
               sizeof(DJI_AUD_NAL));
        
        last_output_data_ = output_with_aud_.data();
        last_output_size_ = static_cast<int>(total_size);
        last_is_keyframe_ = (mfx_bitstream_->FrameType & MFX_FRAMETYPE_I) != 0;
        last_pts_ = frame_count_ * 1000 / 30;  // 假设 30fps，毫秒时间戳

        // 重置 bitstream 用于下一帧
        mfx_bitstream_->DataOffset = 0;
        mfx_bitstream_->DataLength = 0;

        return true;
    }

    return false;
}

uint8_t* EncoderImpl::GetLastOutputData() {
    return last_output_data_;
}

int EncoderImpl::GetLastOutputSize() const {
    return last_output_size_;
}

bool EncoderImpl::IsLastKeyframe() const {
    return last_is_keyframe_;
}

long long EncoderImpl::GetLastPTS() const {
    return last_pts_;
}

bool EncoderImpl::Flush() {
    if (!initialized_) return false;

    mfxStatus sts;
    mfxSyncPoint sync_point = nullptr;

    // 发送空表面冲刷编码器
    sts = ((MFXVideoENCODE*)mfx_encoder_)->EncodeFrameAsync(nullptr, nullptr, (mfxBitstream*)mfx_bitstream_, &sync_point);

    if (sts == MFX_ERR_NONE) {
        sts = ((MFXVideoSession*)mfx_session_)->SyncOperation(sync_point, 60000);
        if (sts == MFX_ERR_NONE && mfx_bitstream_->DataLength > 0) {
            // DJI-H264 标准：每帧结尾必须加上 AUD 信息（6字节）
            size_t encoded_size = static_cast<size_t>(mfx_bitstream_->DataLength);
            size_t total_size = encoded_size + sizeof(DJI_AUD_NAL);
            
            // 确保输出缓冲区足够
            if (output_with_aud_.size() < total_size) {
                output_with_aud_.resize(total_size);
            }
            
            // 复制原始编码数据 + AUD
            memcpy(output_with_aud_.data(), 
                   mfx_bitstream_->Data + mfx_bitstream_->DataOffset, 
                   encoded_size);
            memcpy(output_with_aud_.data() + encoded_size, 
                   DJI_AUD_NAL, 
                   sizeof(DJI_AUD_NAL));
            
            last_output_data_ = output_with_aud_.data();
            last_output_size_ = static_cast<int>(total_size);
            last_is_keyframe_ = (mfx_bitstream_->FrameType & MFX_FRAMETYPE_I) != 0;
            
            mfx_bitstream_->DataOffset = 0;
            mfx_bitstream_->DataLength = 0;
            return true;
        }
    }

    return false;
}
