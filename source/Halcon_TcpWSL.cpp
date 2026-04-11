#include "Halcon_TcpWSL.h"

#include "network.h"

#include "HalconCpp.h"
using namespace HalconCpp;

// ==================== H264编码器全局变量 ====================
#include "dji_encoder.h"
static DJIEncoderHandle g_h264_encoder = nullptr;  // H264编码器句柄
static bool g_encoder_initialized = false;         // 编码器初始化标志

// 编码器配置（默认：30fps, 6000kbps）
static DJIEncoderConfig g_encoder_config = {30, 6000, 30};

// 图像尺寸常量（编码器固定1280x720）
static const int H264_WIDTH = 1280;
static const int H264_HEIGHT = 720;
static const int H264_FRAME_SIZE = H264_WIDTH * H264_HEIGHT;  // 921600 字节每平面

Herror HCreateConnection(Hproc_handle proc_handle)
{
    Hcpar ctxrtuIndex;
    Hcpar ip;
    Hcpar port;
    Hcpar is_server;

    HAllocStringMem(proc_handle, 32);
    HGetSPar(proc_handle, 1, STRING_PAR, &ip, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &port, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &is_server, 1);
    int ret = CreateConnection(ip.par.s, (uint16_t)port.par.l, (bool)is_server.par.l);
    
    // ==================== 初始化H264编码器 ====================
    if (ret == 0 && !g_encoder_initialized)
    {
        g_h264_encoder = DJI_Encoder_Create();
        if (g_h264_encoder)
        {
            int enc_ret = DJI_Encoder_Init(g_h264_encoder, &g_encoder_config);
            if (enc_ret == DJI_OK)
            {
                g_encoder_initialized = true;
            }
            else
            {
                // 初始化失败，销毁编码器
                DJI_Encoder_Destroy(g_h264_encoder);
                g_h264_encoder = nullptr;
                // 不返回错误，让TCP连接继续工作（只是没有H264功能）
            }
        }
    }

    if (ret != 0)
    {
        return 10000 - ret;
    }
    else
    {
        int64_t socket_id = (int64_t)ret;
        HPutElem(proc_handle, 1, &socket_id, 1, LONG_PAR);
        return H_MSG_TRUE;
    }
}
// int RecvData(int socket_id, char** jsontext, char** data, size_t* out_length, int timeout_ms);

Herror HRecvData(Hproc_handle proc_handle)
{
    Hcpar socket_id;
    Hcpar timeout_ms;
    HGetSPar(proc_handle, 1, LONG_PAR, &socket_id, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &timeout_ms, 1);
    Hcpar *dict;
    INT4_8 num;
    HGetPPar(proc_handle, 3, &dict, &num);
    HTuple hv_DictHandle(dict, 1);

    char *jsontext = nullptr;
    char *data = nullptr;
    size_t out_length = 0;
    int ret = RecvData(socket_id.par.l, &jsontext, &data, &out_length, timeout_ms.par.l);
    if (ret != 0)
    {
        return 10000 + ret;
    }
    else
    {
        HTuple h_jsontext(jsontext);
        HTuple dict_json;
        JsonToDict(h_jsontext, HTuple(), HTuple(), &dict_json);
        HTuple CMD;
        GetDictTuple(dict_json, "CMD", &CMD);
        if (CMD.L() == 0)
        {
            HTuple Data;
            GetDictTuple(dict_json, "Data", &Data);
            HTuple 宽;
            HTuple 高;
            HTuple 位深;
            HTuple 通道;
            GetDictTuple(Data, u8"宽", &宽);
            GetDictTuple(Data, u8"高", &高);
            GetDictTuple(Data, u8"位深", &位深);
            GetDictTuple(Data, u8"通道", &通道);
            SetDictTuple(hv_DictHandle, u8"命令", dict_json);
            HObject Image;

            // char *output_str = (char *)HAlloc(proc_handle, 1024);
            if (通道.L() == 1)
            {
                if (位深.L() == 1)
                {
                    GenImage1(&Image, "byte", 宽.L(), 高.L(), (__int64)data);
                }
                else
                {
                    GenImage1(&Image, "uint2", 宽.L(), 高.L(), (__int64)data);
                }
            }
            else
            {
                int64_t Tw = 宽.L() * 高.L() * 位深.L();

                HObject ImageR;
                HObject ImageG;
                HObject ImageB;

                if (位深.L() == 1)
                {
                    GenImage1(&ImageR, "byte", 宽.L(), 高.L(), (__int64)data);
                    GenImage1(&ImageG, "byte", 宽.L(), 高.L(), (__int64)(data + Tw));
                    GenImage1(&ImageB, "byte", 宽.L(), 高.L(), (__int64)(data + Tw * 2));

                    // GenImage3(&Image, "byte", 宽, 高,  (__int64)data,  (__int64)data + Tw,  (__int64)data + Tw * 2);
                }
                else
                {
                    GenImage1(&ImageR, "uint2", 宽.L(), 高.L(), (__int64)data);
                    GenImage1(&ImageG, "uint2", 宽.L(), 高.L(), (__int64)(data + Tw));
                    GenImage1(&ImageB, "uint2", 宽.L(), 高.L(), (__int64)(data + Tw * 2));
                    // GenImage3(&Image, "uint2", 宽, 高,  (__int64)data,  (__int64)data + Tw,  (__int64)data + Tw * 2);
                }
                Compose3(ImageR, ImageG, ImageB, &Image);
            }
            SetDictObject(Image, hv_DictHandle, u8"图");
        }
        else
        {

            SetDictTuple(hv_DictHandle, u8"命令", dict_json);
        }
        free(jsontext);
        free(data);
        return H_MSG_TRUE;
    }
}

// int SendData(int socket_id, const char* jsontext, const char* data, size_t length);
Herror HSendData(Hproc_handle proc_handle)
{
    // 获取 socket_id（第1个输入参数）
    Hcpar socket_id;
    HGetSPar(proc_handle, 1, LONG_PAR, &socket_id, 1);

    // 获取字典（第2个输入参数）
    Hcpar *dict;
    INT4_8 num;
    HGetPPar(proc_handle, 2, &dict, &num); // 注意：这里是参数2！

    HTuple hv_DictHandle(dict, 1);
    HTuple dict_json;
    GetDictTuple(hv_DictHandle, u8"命令", &dict_json);
    HTuple Text_json;
    DictToJson(dict_json, HTuple(), HTuple(), &Text_json);
    // const char *jsontext = Text_json.S();

    HTuple CMD;
    GetDictTuple(dict_json, "CMD", &CMD);

    const char *data = nullptr;
    size_t length = 0;

    if (CMD.L() == 0) // 是图像数据
    {
        HObject Image;
        GetDictObject(&Image, hv_DictHandle, u8"图");

        HTuple Data;
        GetDictTuple(dict_json, "Data", &Data);
        HTuple 宽;
        HTuple 高;
        HTuple TYPE位深;

        HTuple 位深;
        HTuple 通道;

        GetDictTuple(Data, u8"通道", &通道);
        GetDictTuple(Data, u8"位深", &位深);
        char const *Fdata = nullptr;
        int ret = -1;
        if (通道.L() == 1)
        {
            HTuple ptr;

            GetImagePointer1(Image, &ptr, &TYPE位深, &宽, &高);
            Fdata = (const char *)ptr.L();
            length = 宽.L() * 高.L() * 位深.L();
            ret = SendData(socket_id.par.l, Text_json.S(), Fdata, length);
        }
        else if(通道.L() == 3)
        {
            // 分别获取三个通道指针
            // HObject ImgR, ImgG, ImgB;
            // AccessChannel(Image, &ImgR, 1);
            // AccessChannel(Image, &ImgG, 2);
            // AccessChannel(Image, &ImgB, 3);

            HTuple ptrR, ptrG, ptrB;
            GetImagePointer3(Image, &ptrR, &ptrG, &ptrB, &TYPE位深, &宽, &高);

            // 分配连续内存并拼接（planar 格式：RRR...GGG...BBB...）
            length = 宽.L() * 高.L() * 位深.L() * 通道.L();
            unsigned char *buffer = new unsigned char[length];

            memcpy(buffer, (char *)ptrR.L(), 宽.L() * 高.L() * 位深.L());
            memcpy(buffer + 宽.L() * 高.L() * 位深.L(), (char *)ptrG.L(), 宽.L() * 高.L() * 位深.L());
            memcpy(buffer + 宽.L() * 高.L() * 位深.L() * 2, (char *)ptrB.L(), 宽.L() * 高.L() * 位深.L());

            Fdata = (const char *)buffer;
            ret = SendData(socket_id.par.l, Text_json.S(), Fdata, length);
            delete[] buffer;
        }
        else if(通道.L() == 264)  // ==================== H264编码模式 ====================
        {
            // 检查编码器是否已初始化
            if (!g_encoder_initialized || g_h264_encoder == nullptr)
            {
                return 10000 + 999;  // H264编码器未初始化错误
            }

            HTuple ptrR, ptrG, ptrB;
            GetImagePointer3(Image, &ptrR, &ptrG, &ptrB, &TYPE位深, &宽, &高);
            
            // 检查图像尺寸是否符合编码器要求（固定1280x720）
            if (宽.L() != H264_WIDTH || 高.L() != H264_HEIGHT)
            {
                return 10000 + 998;  // 图像尺寸不符合要求错误
            }

            // 准备编码器输入（R/G/B三平面）
            DJIEncoderInput input;
            input.r_plane = (const uint8_t*)ptrR.L();
            input.g_plane = (const uint8_t*)ptrG.L();
            input.b_plane = (const uint8_t*)ptrB.L();

            // 编码一帧
            DJIEncoderOutput output;
            int enc_ret = DJI_Encoder_Encode(g_h264_encoder, &input, &output);
            
            if (enc_ret != DJI_OK)
            {
                // 编码失败
                return 10000 + 500 + enc_ret;  // 500+表示H264编码错误
            }

            // 发送H264编码后的数据
            // output.data 指向DLL内部缓冲区，output.size 是数据长度
            Fdata = (const char*)output.data;
            length = output.size;
            
            // 发送数据（注意：这里需要复制数据，因为output.data在下次Encode时会被覆盖）
            // 但SendData会立即发送，所以可以直接使用
            ret = SendData(socket_id.par.l, Text_json.S(), Fdata, length);
            
            // 注意：不需要delete buffer，因为output.data是DLL内部缓冲区
        }
        if (ret != 0)
        {
            return 10000 + ret; // 自定义错误码
        }
    }
    else
    {
        int ret = SendData(socket_id.par.l, Text_json.S(), nullptr, 0);
        if (ret != 0)
        {
            return 10000 + ret; // 自定义错误码
        }
    }

    return H_MSG_TRUE;
}

// 关闭网络连接并销毁H264编码器
Herror CCloseConnection(Hproc_handle proc_handle)
{
    // 获取 socket_id（第1个输入参数）
    Hcpar socket_id;
    HGetSPar(proc_handle, 1, LONG_PAR, &socket_id, 1);
    
    // 关闭网络连接
    CloseConnection((int)socket_id.par.l);
    
    // ==================== 销毁H264编码器 ====================
    if (g_h264_encoder != nullptr)
    {
        DJI_Encoder_Destroy(g_h264_encoder);
        g_h264_encoder = nullptr;
        g_encoder_initialized = false;
    }
    
    return H_MSG_TRUE;
}
