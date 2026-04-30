#pragma once

// 内部 C++ 实现头文件，不对外暴露

#include <cstdint>
#include <vector>
#include <memory>

// oneVPL 头文件（内部使用）
#include <vpl/mfxvideo++.h>
#include <vpl/mfxcommon.h>

#ifdef _WIN32
// D3D11 前向声明
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
#endif

// 内部实现类
class EncoderImpl {
public:
    EncoderImpl();
    ~EncoderImpl();

    // 禁止拷贝
    EncoderImpl(const EncoderImpl&) = delete;
    EncoderImpl& operator=(const EncoderImpl&) = delete;

    // 初始化（固定 1280x720）
    bool Initialize(int width, int height, int fps, int bitrate_kbps);

    // 编码一帧
    // 返回：是否成功，编码后的数据通过 GetLastOutput 获取
    bool EncodeFrame(const uint8_t* r_plane, const uint8_t* g_plane, const uint8_t* b_plane);

    // 获取最近一次编码的输出数据（仅在下次 EncodeFrame 前有效）
    uint8_t* GetLastOutputData();
    int GetLastOutputSize() const;
    bool IsLastKeyframe() const;
    long long GetLastPTS() const;

    // 是否已初始化
    bool IsInitialized() const { return initialized_; }

    // 冲刷编码器，获取缓冲的帧
    // 返回：true 表示有输出数据，false 表示没有更多数据
    bool Flush();

private:
    bool InitD3D11();
    bool InitVPL();
    bool CreateEncoder(int fps, int bitrate_kbps);
    bool RGBToNV12(const uint8_t* r_plane, const uint8_t* g_plane, const uint8_t* b_plane);
    bool ProcessEncoding();

#ifdef _WIN32
    // D3D11
    ID3D11Device* d3d11_device_ = nullptr;
    ID3D11DeviceContext* d3d11_context_ = nullptr;
    ID3D11Texture2D* d3d11_texture_ = nullptr;
#else
    void* d3d11_device_ = nullptr;
    void* d3d11_context_ = nullptr;
    void* d3d11_texture_ = nullptr;
#endif

    // oneVPL
    MFXVideoSession* mfx_session_ = nullptr;
    MFXVideoENCODE* mfx_encoder_ = nullptr;

    // 视频参数
    mfxVideoParam* encode_param_ = nullptr;
    mfxBitstream* mfx_bitstream_ = nullptr;
    mfxSyncPoint sync_point_ = nullptr;
    
    // DJI-H264 标准：每帧结尾的 AUD NAL 单元（6字节）
    static constexpr uint8_t DJI_AUD_NAL[6] = {0x00, 0x00, 0x00, 0x01, 0x09, 0x10};

    // 缓冲区
    std::vector<uint8_t> nv12_buffer_;
    std::vector<uint8_t> bitstream_buffer_;
    std::vector<uint8_t> output_with_aud_;  // 原始数据 + AUD（符合DJI-H264标准）

    // 输出状态（保存最后一次编码结果）
    uint8_t* last_output_data_ = nullptr;
    int last_output_size_ = 0;
    bool last_is_keyframe_ = false;
    long long last_pts_ = 0;

    // 状态
    bool initialized_ = false;
    int width_ = 1280;
    int height_ = 720;
    int gop_size_ = 30;
    int frame_count_ = 0;
};
