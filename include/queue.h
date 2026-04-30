#ifndef QUEUE_H
#define QUEUE_H

#include <mutex>
#include <condition_variable>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "network_config.h"

// 帧头结构
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      // 魔数 0xDEADBEEF
    uint32_t json_len;   // JSON字符串长度（含\0）
    uint32_t data_len;   // 二进制数据长度
} Header;
#pragma pack(pop)

// 数据包结构
typedef struct {
    Header header;
    char* json_str;      // 已带 \0 结尾
    char* binary_data;
    size_t data_len;
} Packet;

// 线程安全环形队列
typedef struct {
    Packet* buffer[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    std::mutex mtx;
    std::condition_variable not_empty;
} PacketQueue;

// 初始化队列
static inline void QueueInit(PacketQueue* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

// 入队（满时自动丢弃最旧数据包）
static inline bool QueuePush(PacketQueue* q, Packet* packet) {
    std::unique_lock<std::mutex> lock(q->mtx);
    
    // 如果队列已满，丢弃最旧的包
    if (q->count >= MAX_QUEUE_SIZE) {
        Packet* old_packet = q->buffer[q->head];
        if (old_packet) {
            if (old_packet->json_str) free(old_packet->json_str);
            if (old_packet->binary_data) free(old_packet->binary_data);
            free(old_packet);
        }
        q->head = (q->head + 1) % MAX_QUEUE_SIZE;
        q->count--;
    }
    
    // 入队新包
    q->buffer[q->tail] = packet;
    q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
    q->count++;
    
    lock.unlock();
    q->not_empty.notify_one();
    return true;
}

// 出队（带超时）
static inline Packet* QueuePop(PacketQueue* q, int timeout_ms) {
    std::unique_lock<std::mutex> lock(q->mtx);
    
    if (timeout_ms < 0) {
        // 永久阻塞
        q->not_empty.wait(lock, [q] { return q->count > 0; });
    } else if (timeout_ms > 0) {
        // 超时等待
        if (!q->not_empty.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                                  [q] { return q->count > 0; })) {
            return nullptr;  // 超时
        }
    } else {
        // 非阻塞
        if (q->count == 0) {
            return nullptr;
        }
    }
    
    if (q->count == 0) {
        return nullptr;
    }
    
    Packet* packet = q->buffer[q->head];
    q->buffer[q->head] = nullptr;
    q->head = (q->head + 1) % MAX_QUEUE_SIZE;
    q->count--;
    
    return packet;
}

// 清空队列
static inline void QueueClear(PacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    
    while (q->count > 0) {
        Packet* packet = q->buffer[q->head];
        if (packet) {
            if (packet->json_str) free(packet->json_str);
            if (packet->binary_data) free(packet->binary_data);
            free(packet);
        }
        q->head = (q->head + 1) % MAX_QUEUE_SIZE;
        q->count--;
    }
    
    q->head = 0;
    q->tail = 0;
}

// 获取队列当前大小
static inline int QueueSize(PacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    return q->count;
}

#endif // QUEUE_H
