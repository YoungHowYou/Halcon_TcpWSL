#pragma once

// 内部 C++ 实现头文件，不对外暴露

#include <cstdint>
#include <vector>

// oneVPL 头文件（内部使用）
#include <vpl/mfxvideo++.h>
#include <vpl/mfxcommon.h>

// 内部解码器实现类
class DecoderImpl {
public:
    DecoderImpl();
    ~DecoderImpl();

    // 禁止拷贝
    DecoderImpl(const DecoderImpl&) = delete;
    DecoderImpl& operator=(const DecoderImpl&) = delete;

    // 初始化 VPL 会话（解码器在首帧到来时自动创建）
    bool Initialize();

    // 解码一帧 H264 数据
    // 返回：true 表示有解码后的图像可用，false 表示需要更多数据或失败
    bool DecodeFrame(const uint8_t* h264_data, int h264_size);

    // 获取解码后的 R/G/B 平面（仅在下次 DecodeFrame 前有效）
    uint8_t* GetOutputR() { return r_plane_.data(); }
    uint8_t* GetOutputG() { return g_plane_.data(); }
    uint8_t* GetOutputB() { return b_plane_.data(); }
    int GetOutputWidth() const { return width_; }
    int GetOutputHeight() const { return height_; }

    bool IsInitialized() const { return initialized_; }

    // 冲刷解码器，获取缓冲的帧
    bool Flush();

private:
    bool InitVPL();
    bool InitDecoder(const uint8_t* h264_data, int h264_size);
    bool AllocSurfaces();
    bool NV12ToRGB(mfxFrameSurface1* surface);
    int GetFreeSurfaceIndex();

    // oneVPL
    MFXVideoSession* mfx_session_ = nullptr;
    MFXVideoDECODE* mfx_decoder_ = nullptr;
    mfxVideoParam* decode_param_ = nullptr;

    // Surface 池
    std::vector<mfxFrameSurface1> surfaces_;
    std::vector<std::vector<uint8_t>> surface_buffers_;
    int num_surfaces_ = 0;

    // 解码输出 RGB 平面
    std::vector<uint8_t> r_plane_;
    std::vector<uint8_t> g_plane_;
    std::vector<uint8_t> b_plane_;

    // Bitstream 缓冲区
    std::vector<uint8_t> bs_buffer_;
    mfxBitstream mfx_bitstream_;

    // 状态
    bool initialized_ = false;
    bool decoder_created_ = false;
    int width_ = 0;
    int height_ = 0;
};
