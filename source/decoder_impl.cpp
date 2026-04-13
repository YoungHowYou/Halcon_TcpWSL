#include "decoder_impl.h"

// Windows 头文件
#include <windows.h>

// libyuv 头文件
#include <libyuv.h>

// 标准库
#include <cstring>
#include <iostream>

// 对齐宏
constexpr int ALIGN16_DEC(int x) { return (x + 15) & ~15; }

DecoderImpl::DecoderImpl() {
    std::memset(&mfx_bitstream_, 0, sizeof(mfxBitstream));
}

DecoderImpl::~DecoderImpl() {
    if (mfx_decoder_) {
        mfx_decoder_->Close();
        delete mfx_decoder_;
    }
    if (mfx_session_) {
        mfx_session_->Close();
        delete mfx_session_;
    }
    delete decode_param_;
}

bool DecoderImpl::Initialize() {
    if (!InitVPL()) {
        std::cerr << "[DecoderImpl] VPL initialization failed" << std::endl;
        return false;
    }

    // 分配 bitstream 缓冲区（4MB，足够一个 IDR 帧）
    bs_buffer_.resize(4 * 1024 * 1024);
    mfx_bitstream_.Data = bs_buffer_.data();
    mfx_bitstream_.MaxLength = static_cast<mfxU32>(bs_buffer_.size());
    mfx_bitstream_.DataOffset = 0;
    mfx_bitstream_.DataLength = 0;

    initialized_ = true;
    std::cout << "[DecoderImpl] Initialized, waiting for first keyframe" << std::endl;
    return true;
}

bool DecoderImpl::InitVPL() {
    mfxStatus sts;

    mfx_session_ = new MFXVideoSession();
    mfxVersion version = {{1, 0}};

    // 尝试硬件加速
    sts = mfx_session_->Init(MFX_IMPL_HARDWARE_ANY, &version);
    if (sts == MFX_ERR_NONE) {
        std::cout << "[DecoderImpl] Using hardware acceleration" << std::endl;
    } else {
        // 软件回退
        std::cerr << "[DecoderImpl] Hardware init failed: " << sts << ", falling back to software" << std::endl;
        sts = mfx_session_->Init(MFX_IMPL_SOFTWARE, &version);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "[DecoderImpl] Failed to initialize VPL session: " << sts << std::endl;
            return false;
        }
        std::cout << "[DecoderImpl] Using software VPL implementation" << std::endl;
    }

    return true;
}

bool DecoderImpl::InitDecoder(const uint8_t* h264_data, int h264_size) {
    mfxStatus sts;

    // 将 H264 数据填入 bitstream
    if (static_cast<mfxU32>(h264_size) > mfx_bitstream_.MaxLength) {
        bs_buffer_.resize(h264_size * 2);
        mfx_bitstream_.Data = bs_buffer_.data();
        mfx_bitstream_.MaxLength = static_cast<mfxU32>(bs_buffer_.size());
    }
    std::memcpy(mfx_bitstream_.Data, h264_data, h264_size);
    mfx_bitstream_.DataOffset = 0;
    mfx_bitstream_.DataLength = h264_size;

    // 解码参数
    decode_param_ = new mfxVideoParam();
    std::memset(decode_param_, 0, sizeof(mfxVideoParam));
    decode_param_->mfx.CodecId = MFX_CODEC_AVC;
    decode_param_->IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    // 从 bitstream 解析 SPS/PPS 获取分辨率信息
    sts = MFXVideoDECODE_DecodeHeader(*(mfx_session_), &mfx_bitstream_, decode_param_);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "[DecoderImpl] DecodeHeader failed: " << sts << std::endl;
        return false;
    }

    width_ = decode_param_->mfx.FrameInfo.CropW;
    height_ = decode_param_->mfx.FrameInfo.CropH;
    if (width_ == 0) width_ = decode_param_->mfx.FrameInfo.Width;
    if (height_ == 0) height_ = decode_param_->mfx.FrameInfo.Height;

    std::cout << "[DecoderImpl] Detected resolution: " << width_ << "x" << height_ << std::endl;

    // 确保帧格式为 NV12
    decode_param_->mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    decode_param_->mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    decode_param_->AsyncDepth = 1;

    // 创建解码器
    mfx_decoder_ = new MFXVideoDECODE(*mfx_session_);

    // 分配 surface 池
    if (!AllocSurfaces()) {
        std::cerr << "[DecoderImpl] Failed to allocate surfaces" << std::endl;
        return false;
    }

    // 初始化解码器
    sts = mfx_decoder_->Init(decode_param_);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::cerr << "[DecoderImpl] Decoder init failed: " << sts << std::endl;
        return false;
    }

    // 分配 RGB 输出缓冲区
    size_t plane_size = static_cast<size_t>(width_) * height_;
    r_plane_.resize(plane_size);
    g_plane_.resize(plane_size);
    b_plane_.resize(plane_size);

    decoder_created_ = true;
    std::cout << "[DecoderImpl] Decoder created: " << width_ << "x" << height_ << std::endl;
    return true;
}

bool DecoderImpl::AllocSurfaces() {
    mfxStatus sts;

    // 查询解码器需要的 surface 数量
    mfxFrameAllocRequest request = {};
    sts = mfx_decoder_->QueryIOSurf(decode_param_, &request);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::cerr << "[DecoderImpl] QueryIOSurf failed: " << sts << std::endl;
        // 使用默认数量
        request.NumFrameSuggested = 8;
    }

    num_surfaces_ = request.NumFrameSuggested + 4;  // 额外留几个
    if (num_surfaces_ < 8) num_surfaces_ = 8;

    int aligned_width = ALIGN16_DEC(decode_param_->mfx.FrameInfo.Width);
    int aligned_height = ALIGN16_DEC(decode_param_->mfx.FrameInfo.Height);
    size_t frame_size = static_cast<size_t>(aligned_width) * aligned_height * 3 / 2;  // NV12

    surfaces_.resize(num_surfaces_);
    surface_buffers_.resize(num_surfaces_);

    for (int i = 0; i < num_surfaces_; i++) {
        surface_buffers_[i].resize(frame_size, 0);

        std::memset(&surfaces_[i], 0, sizeof(mfxFrameSurface1));
        surfaces_[i].Info = decode_param_->mfx.FrameInfo;
        surfaces_[i].Data.Y = surface_buffers_[i].data();
        surfaces_[i].Data.UV = surface_buffers_[i].data() + aligned_width * aligned_height;
        surfaces_[i].Data.Pitch = aligned_width;
    }

    std::cout << "[DecoderImpl] Allocated " << num_surfaces_ << " surfaces ("
              << aligned_width << "x" << aligned_height << ")" << std::endl;
    return true;
}

int DecoderImpl::GetFreeSurfaceIndex() {
    for (int i = 0; i < num_surfaces_; i++) {
        if (surfaces_[i].Data.Locked == 0) {
            return i;
        }
    }
    return -1;
}

bool DecoderImpl::NV12ToRGB(mfxFrameSurface1* surface) {
    int crop_x = surface->Info.CropX;
    int crop_y = surface->Info.CropY;
    int crop_w = surface->Info.CropW;
    int crop_h = surface->Info.CropH;
    if (crop_w == 0) crop_w = surface->Info.Width;
    if (crop_h == 0) crop_h = surface->Info.Height;

    int pitch = surface->Data.Pitch;

    // Y 和 UV 平面的起始地址（考虑 crop 偏移）
    uint8_t* y_ptr = surface->Data.Y + crop_y * pitch + crop_x;
    uint8_t* uv_ptr = surface->Data.UV + (crop_y / 2) * pitch + crop_x;

    // NV12 转 ARGB
    std::vector<uint8_t> argb(crop_w * crop_h * 4);
    int ret = libyuv::NV12ToARGB(
        y_ptr, pitch,
        uv_ptr, pitch,
        argb.data(), crop_w * 4,
        crop_w, crop_h
    );
    if (ret != 0) {
        std::cerr << "[DecoderImpl] NV12 to ARGB conversion failed" << std::endl;
        return false;
    }

    // ARGB 拆分为 R/G/B 平面
    int pixels = crop_w * crop_h;
    for (int i = 0; i < pixels; i++) {
        b_plane_[i] = argb[i * 4 + 0];  // B
        g_plane_[i] = argb[i * 4 + 1];  // G
        r_plane_[i] = argb[i * 4 + 2];  // R
    }

    width_ = crop_w;
    height_ = crop_h;
    return true;
}

bool DecoderImpl::DecodeFrame(const uint8_t* h264_data, int h264_size) {
    if (!initialized_) return false;
    if (!h264_data || h264_size <= 0) return false;

    // 首次解码：需要先从关键帧初始化解码器
    if (!decoder_created_) {
        if (!InitDecoder(h264_data, h264_size)) {
            return false;
        }
        // InitDecoder 已经将数据填入 bitstream，继续解码
    } else {
        // 将新数据追加到 bitstream 缓冲区
        mfxU32 needed = mfx_bitstream_.DataOffset + mfx_bitstream_.DataLength + h264_size;
        if (needed > mfx_bitstream_.MaxLength) {
            // 先移动剩余数据到缓冲区头部
            if (mfx_bitstream_.DataOffset > 0 && mfx_bitstream_.DataLength > 0) {
                std::memmove(mfx_bitstream_.Data,
                             mfx_bitstream_.Data + mfx_bitstream_.DataOffset,
                             mfx_bitstream_.DataLength);
                mfx_bitstream_.DataOffset = 0;
            } else if (mfx_bitstream_.DataLength == 0) {
                mfx_bitstream_.DataOffset = 0;
            }

            // 如果还不够大，扩容
            needed = mfx_bitstream_.DataLength + h264_size;
            if (needed > mfx_bitstream_.MaxLength) {
                bs_buffer_.resize(needed * 2);
                mfx_bitstream_.Data = bs_buffer_.data();
                mfx_bitstream_.MaxLength = static_cast<mfxU32>(bs_buffer_.size());
            }
        }

        std::memcpy(mfx_bitstream_.Data + mfx_bitstream_.DataOffset + mfx_bitstream_.DataLength,
                     h264_data, h264_size);
        mfx_bitstream_.DataLength += h264_size;
    }

    // 解码
    mfxStatus sts;
    mfxSyncPoint sync_point = nullptr;
    mfxFrameSurface1* out_surface = nullptr;

    int surface_idx = GetFreeSurfaceIndex();
    if (surface_idx < 0) {
        std::cerr << "[DecoderImpl] No free surface available" << std::endl;
        return false;
    }

    sts = mfx_decoder_->DecodeFrameAsync(
        &mfx_bitstream_,
        &surfaces_[surface_idx],
        &out_surface,
        &sync_point
    );

    if (sts == MFX_ERR_MORE_DATA) {
        // 需要更多数据，不是错误
        return false;
    } else if (sts == MFX_ERR_MORE_SURFACE) {
        // 需要更多 surface，重试
        surface_idx = GetFreeSurfaceIndex();
        if (surface_idx < 0) return false;
        sts = mfx_decoder_->DecodeFrameAsync(
            &mfx_bitstream_,
            &surfaces_[surface_idx],
            &out_surface,
            &sync_point
        );
    } else if (sts == MFX_WRN_DEVICE_BUSY) {
        int retries = 0;
        while (sts == MFX_WRN_DEVICE_BUSY && retries < 100) {
            Sleep(1);
            sts = mfx_decoder_->DecodeFrameAsync(
                &mfx_bitstream_,
                &surfaces_[surface_idx],
                &out_surface,
                &sync_point
            );
            retries++;
        }
    }

    if (sts != MFX_ERR_NONE) {
        if (sts == MFX_WRN_VIDEO_PARAM_CHANGED) {
            // 参数变化，继续处理
        } else {
            std::cerr << "[DecoderImpl] DecodeFrameAsync failed: " << sts << std::endl;
            return false;
        }
    }

    if (!sync_point || !out_surface) {
        return false;
    }

    // 等待同步完成
    sts = mfx_session_->SyncOperation(sync_point, 60000);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "[DecoderImpl] SyncOperation failed: " << sts << std::endl;
        return false;
    }

    // NV12 转 RGB
    if (!NV12ToRGB(out_surface)) {
        return false;
    }

    return true;
}

bool DecoderImpl::Flush() {
    if (!initialized_ || !decoder_created_) return false;

    mfxStatus sts;
    mfxSyncPoint sync_point = nullptr;
    mfxFrameSurface1* out_surface = nullptr;

    int surface_idx = GetFreeSurfaceIndex();
    if (surface_idx < 0) return false;

    // 传入 nullptr bitstream 冲刷解码器
    sts = mfx_decoder_->DecodeFrameAsync(
        nullptr,
        &surfaces_[surface_idx],
        &out_surface,
        &sync_point
    );

    if (sts == MFX_ERR_NONE && sync_point && out_surface) {
        sts = mfx_session_->SyncOperation(sync_point, 60000);
        if (sts == MFX_ERR_NONE) {
            return NV12ToRGB(out_surface);
        }
    }

    return false;
}
