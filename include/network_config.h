#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

// 最大队列深度（发送队列 + 接收队列 各自独立）
#ifndef MAX_QUEUE_SIZE
#define MAX_QUEUE_SIZE 8  // 默认缓存 8 个完整数据包
#endif

// Windows端配置
#define WIN_DEFAULT_BACKLOG 10
#define WIN_RECV_BUFFER_SIZE 4096

#endif // NETWORK_CONFIG_H
