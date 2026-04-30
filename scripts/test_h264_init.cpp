// Smoke test：验证 H264 编码器能在当前机器初始化成功
// 编译方式见 scripts/install_linux.sh 末尾的 smoke test 段
#include <vpl/mfxvideo++.h>
#include <vpl/mfxstructures.h>
#include <iostream>
#include <cstring>

constexpr int ALIGN16(int x) { return (x + 15) & ~15; }

static int run() {
    MFXVideoSession s;
    mfxVersion v = {{1, 0}};

    auto sts = s.Init(MFX_IMPL_HARDWARE_ANY, &v);
    std::cout << "[1/3] MFX Init (HARDWARE_ANY): " << sts << "\n";
    if (sts != MFX_ERR_NONE) {
        std::cout << "    -> 失败：检查 libmfx-gen1.2/intel-media-va-driver"
                     " 是否装好、当前用户是否在 render 组\n";
        return 1;
    }

    MFXVideoENCODE enc(s);
    mfxVideoParam p = {};
    p.mfx.CodecId = MFX_CODEC_AVC;
    p.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    p.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
    p.mfx.TargetKbps = 6000;
    p.mfx.GopPicSize = 30;
    p.mfx.GopRefDist = 1;
    p.mfx.NumRefFrame = 1;
    p.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    p.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    p.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    p.mfx.FrameInfo.FrameRateExtN = 30;
    p.mfx.FrameInfo.FrameRateExtD = 1;
    p.mfx.FrameInfo.Width = ALIGN16(1280);
    p.mfx.FrameInfo.Height = ALIGN16(720);
    p.mfx.FrameInfo.CropW = 1280;
    p.mfx.FrameInfo.CropH = 720;
    p.AsyncDepth = 1;
    p.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    mfxVideoParam q = p;
    sts = enc.Query(&p, &q);
    std::cout << "[2/3] MFXVideoENCODE::Query: " << sts << "\n";
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::cout << "    -> 失败：GPU 设备打开失败，"
                     "通常是 /dev/dri/renderD128 权限问题\n";
        return 2;
    }

    sts = enc.Init(&p);
    std::cout << "[3/3] MFXVideoENCODE::Init: " << sts << "\n";
    if (sts != MFX_ERR_NONE) {
        std::cout << "    -> 失败：编码器实际打开失败\n";
        return 3;
    }

    std::cout << "\n✓ H264 硬件编码器可用\n";
    return 0;
}

int main() {
    try { return run(); }
    catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << "\n"; return 99;
    }
}
