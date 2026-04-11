#ifndef DJI_ENCODER_H
#define DJI_ENCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 导出宏定义 */
#ifdef DJI_ENCODER_EXPORTS
    #define DJI_API __declspec(dllexport)
#else
    #define DJI_API __declspec(dllimport)
#endif

/* 不透明句柄类型（封装 C++ 对象） */
typedef struct DJIEncoderInternal* DJIEncoderHandle;

/* 错误码定义 */
typedef enum {
    DJI_OK = 0,
    DJI_ERROR_INVALID_PARAM = -1,      /* 参数错误（空指针、不支持的分辨率） */
    DJI_ERROR_INIT_FAILED = -2,        /* 初始化失败（GPU 不可用、驱动问题） */
    DJI_ERROR_ENCODE_FAILED = -3,      /* 编码失败 */
    DJI_ERROR_NOT_INITIALIZED = -4,    /* 未初始化就调用编码 */
    DJI_ERROR_OUT_OF_MEMORY = -5       /* 内存分配失败 */
} DJIErrorCode;

/* 编码器配置结构体 */
typedef struct {
    int fps;                /* 帧率：25 或 30（默认 30） */
    int bitrate_kbps;       /* 目标码率：4000-8000 kbps（默认 6000） */
    int gop_size;           /* GOP 长度：默认等于 fps（每秒 1 个 IDR） */
} DJIEncoderConfig;

/* 输入图像结构体（紧凑 RGB 平面） */
typedef struct {
    const uint8_t* r_plane;     /* 红色通道指针，大小 1280×720 字节 */
    const uint8_t* g_plane;     /* 绿色通道指针 */
    const uint8_t* b_plane;     /* 蓝色通道指针 */
    /* 注意：因固定紧凑存储，无需 stride 参数 */
} DJIEncoderInput;

/* 编码输出结构体 */
typedef struct {
    uint8_t* data;          /* H.264 裸流指针（DLL 内部缓冲区，只读） */
    int size;               /* 数据长度（字节） */
    int is_keyframe;        /* 是否为关键帧（IDR）：1 是，0 否 */
    long long pts;          /* 时间戳（毫秒，从初始化开始计算） */
} DJIEncoderOutput;

/* 版本信息 */
#define DJI_ENCODER_VERSION_MAJOR 1
#define DJI_ENCODER_VERSION_MINOR 0
#define DJI_ENCODER_VERSION_PATCH 0

/*=================== API 函数声明 ===================*/

/**
 * @brief 创建编码器实例
 * @return 编码器句柄，失败返回 NULL
 */
DJI_API DJIEncoderHandle DJI_Encoder_Create(void);

/**
 * @brief 初始化编码器（固定 1280×720）
 * @param handle 编码器句柄
 * @param config 配置参数，传 NULL 使用默认（30fps, 6000kbps）
 * @return 错误码，DJI_OK 表示成功
 */
DJI_API int DJI_Encoder_Init(DJIEncoderHandle handle, const DJIEncoderConfig* config);

/**
 * @brief 编码一帧图像
 * @param handle 编码器句柄
 * @param input 输入图像数据（R/G/B 三平面）
 * @param output 输出结构体，由函数填充。注意：output.data 指向内部缓冲区，
 *               仅在下次调用 DJI_Encoder_Encode 或 DJI_Encoder_Destroy 前有效。
 *               如需长期保存，请立即 memcpy 复制。
 * @return 错误码
 */
DJI_API int DJI_Encoder_Encode(DJIEncoderHandle handle, 
                                const DJIEncoderInput* input, 
                                DJIEncoderOutput* output);

/**
 * @brief 冲刷编码器，获取缓冲的帧
 * @param handle 编码器句柄
 * @param output 输出结构体
 * @return 1 表示有输出数据，0 表示没有更多数据，负数表示错误
 */
DJI_API int DJI_Encoder_Flush(DJIEncoderHandle handle, DJIEncoderOutput* output);

/**
 * @brief 销毁编码器，释放所有资源
 * @param handle 编码器句柄，销毁后句柄失效，不可再用
 */
DJI_API void DJI_Encoder_Destroy(DJIEncoderHandle handle);

/**
 * @brief 获取版本字符串（静态字符串，无需释放）
 */
DJI_API const char* DJI_Encoder_GetVersion(void);

/**
 * @brief 获取错误码描述字符串
 */
DJI_API const char* DJI_Encoder_GetErrorString(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* DJI_ENCODER_H */
