#include "Halcon.h"
#ifdef _WIN32
  #define EXPORTS_API __declspec(dllexport)
#else
  #define EXPORTS_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

#pragma region Network

// 创建网络连接
EXPORTS_API Herror HCreateConnection(Hproc_handle proc_handle);

// 接收数据
EXPORTS_API Herror HRecvData(Hproc_handle proc_handle);

// 发送数据
EXPORTS_API Herror HSendData(Hproc_handle proc_handle);

// 关闭网络连接
EXPORTS_API Herror CCloseConnection(Hproc_handle proc_handle);

#pragma endregion

#ifdef __cplusplus
}
#endif
