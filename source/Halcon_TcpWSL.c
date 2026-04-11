#include <stdio.h>
#include "Halcon_TcpWSL.h"

// 创建网络连接的包装器
Herror CCreateConnection(Hproc_handle proc_handle)
{
    return HCreateConnection(proc_handle);
}

// 接收数据的包装器
Herror CRecvData(Hproc_handle proc_handle)
{
    return HRecvData(proc_handle);
}

// 发送数据的包装器
Herror CSendData(Hproc_handle proc_handle)
{
    return HSendData(proc_handle);
}