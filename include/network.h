#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  using SOCKET = int;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 创建连接
// ip: 目标IP地址（客户端）或监听地址（服务端）
// port: 端口号
// is_server: true表示服务端，false表示客户端
// 返回值: 套接字ID（>=0），失败返回-1
int CreateConnection(const char* ip, uint16_t port, bool is_server);

// 发送数据
// socket_id: 套接字ID
// jsontext: JSON格式的元数据字符串（会自动添加\0结尾）
// data: 二进制数据缓冲区
// length: 二进制数据长度
// 返回值: 0表示成功入队，-1表示参数错误
int SendData(int socket_id, const char* jsontext, const char* data, size_t length);

// 接收数据
// socket_id: 套接字ID
// jsontext: 输出参数，返回JSON字符串（需调用者free）
// data: 输出参数，返回二进制数据缓冲区（需调用者free）
// out_length: 输出参数，返回二进制数据长度
// timeout_ms: 超时时间（毫秒）
//      > 0: 等待指定毫秒数
//      = 0: 非阻塞，立即返回
//      < 0: 永久阻塞
// 返回值: 0表示成功，-1表示错误，-2表示超时
int RecvData(int socket_id, char** jsontext, char** data, size_t* out_length, int timeout_ms);

// 关闭连接
// socket_id: 套接字ID
void CloseConnection(int socket_id);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_H
