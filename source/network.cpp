#include "network.h"
#include "network_config.h"
#include "queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <mutex>

#ifdef _WIN32
  #pragma comment(lib, "ws2_32.lib")
#endif

#ifdef _WIN32
static bool g_sockets_initialized = false;
#else
static bool g_sockets_initialized = true;
#endif

enum ConnState
{
    CONN_IDLE,
    CONN_CONNECTED,
    CONN_DISCONNECTED
};

struct Connection
{
    SOCKET sock_fd;
    ConnState state;
    PacketQueue send_queue;
    PacketQueue recv_queue;
    std::thread send_thread;
    std::thread recv_thread;
    bool threads_running;
    struct sockaddr_in addr;
};

#define MAX_CONNECTIONS 64
static Connection g_connections[MAX_CONNECTIONS];
static std::mutex g_conn_mutex;

static int GetLastSocketError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool InitSockets()
{
#ifdef _WIN32
    if (g_sockets_initialized)
    {
        return true;
    }

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        return false;
    }

    g_sockets_initialized = true;
#endif
    return true;
}

static void CloseSocket(SOCKET sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

static int FindFreeSlot()
{
    for (int i = 0; i < MAX_CONNECTIONS; i++)
    {
        if (g_connections[i].state == CONN_IDLE)
        {
            return i;
        }
    }
    return -1;
}

static void SleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void SendThreadFunc(int slot)
{
    Connection *conn = &g_connections[slot];

    while (conn->threads_running && conn->state == CONN_CONNECTED)
    {
        Packet *packet = QueuePop(&conn->send_queue, 100);
        if (!packet)
        {
            continue;
        }

        size_t total_size = sizeof(Header) + packet->header.json_len + packet->header.data_len;
        char *send_buf = (char *)malloc(total_size);
        if (!send_buf)
        {
            if (packet->json_str)
                free(packet->json_str);
            if (packet->binary_data)
                free(packet->binary_data);
            free(packet);
            continue;
        }

        memcpy(send_buf, &packet->header, sizeof(Header));
        if (packet->json_str && packet->header.json_len > 0)
        {
            memcpy(send_buf + sizeof(Header), packet->json_str, (size_t)packet->header.json_len);
        }
        if (packet->binary_data && packet->header.data_len > 0)
        {
            memcpy(send_buf + sizeof(Header) + packet->header.json_len,
                   packet->binary_data, (size_t)packet->header.data_len);
        }

        size_t sent = 0;
        while (sent < total_size && conn->state == CONN_CONNECTED)
        {
            int n = (int)send(conn->sock_fd, send_buf + sent, (int)(total_size - sent), 0);
            if (n == SOCKET_ERROR)
            {
                int err = GetLastSocketError();
                if (err == EINTR
#ifdef _WIN32
                    || err == WSAEINTR
#endif
                   )
                {
                    continue;
                }
                conn->state = CONN_DISCONNECTED;
                QueueClear(&conn->send_queue);
                break;
            }
            else if (n == 0)
            {
                conn->state = CONN_DISCONNECTED;
                QueueClear(&conn->send_queue);
                break;
            }
            sent += n;
        }

        free(send_buf);
        if (packet->json_str)
            free(packet->json_str);
        if (packet->binary_data)
            free(packet->binary_data);
        free(packet);
    }
}

static void RecvThreadFunc(int slot)
{
    Connection *conn = &g_connections[slot];
    char recv_buf[WIN_RECV_BUFFER_SIZE];
    char *frame_buf = nullptr;
    size_t frame_buf_size = 0;
    size_t frame_buf_offset = 0;

    while (conn->threads_running && conn->state == CONN_CONNECTED)
    {
        int n = (int)recv(conn->sock_fd, recv_buf, sizeof(recv_buf), 0);

        if (n == SOCKET_ERROR)
        {
            int err = GetLastSocketError();
            if (err == EINTR
#ifdef _WIN32
                || err == WSAEINTR
#endif
            )
            {
                continue;
            }
            conn->state = CONN_DISCONNECTED;
            break;
        }
        else if (n == 0)
        {
            conn->state = CONN_DISCONNECTED;
            break;
        }

        if (frame_buf_offset + n > frame_buf_size)
        {
            size_t new_size = frame_buf_size * 2;
            if (new_size < 4096)
                new_size = 4096;
            while (new_size < frame_buf_offset + n)
                new_size *= 2;

            char *new_buf = (char *)realloc(frame_buf, new_size);
            if (!new_buf)
            {
                conn->state = CONN_DISCONNECTED;
                break;
            }
            frame_buf = new_buf;
            frame_buf_size = new_size;
        }

        memcpy(frame_buf + frame_buf_offset, recv_buf, n);
        frame_buf_offset += n;

        while (frame_buf_offset >= sizeof(Header))
        {
            Header *header = (Header *)frame_buf;
            if (header->magic != 0xDEADBEEF)
            {
                bool found = false;
                for (size_t i = 1; i <= frame_buf_offset - sizeof(uint32_t); i++)
                {
                    if (*(uint32_t *)(frame_buf + i) == 0xDEADBEEF)
                    {
                        memmove(frame_buf, frame_buf + i, (size_t)(frame_buf_offset - i));
                        frame_buf_offset -= i;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    frame_buf_offset = 0;
                }
                break;
            }

            size_t total_size = sizeof(Header) + header->json_len + header->data_len;
            if (frame_buf_offset < total_size)
            {
                break;
            }

            Packet *packet = (Packet *)malloc(sizeof(Packet));
            if (!packet)
            {
                conn->state = CONN_DISCONNECTED;
                break;
            }

            packet->header = *header;
            packet->data_len = header->data_len;

            if (header->json_len > 0)
            {
                packet->json_str = (char *)malloc((size_t)header->json_len);
                if (!packet->json_str)
                {
                    free(packet);
                    conn->state = CONN_DISCONNECTED;
                    break;
                }
                memcpy(packet->json_str, frame_buf + sizeof(Header), (size_t)header->json_len);
            }
            else
            {
                packet->json_str = nullptr;
            }

            if (header->data_len > 0)
            {
                packet->binary_data = (char *)malloc(header->data_len);
                if (!packet->binary_data)
                {
                    if (packet->json_str)
                        free(packet->json_str);
                    free(packet);
                    conn->state = CONN_DISCONNECTED;
                    break;
                }
                memcpy(packet->binary_data,
                       frame_buf + sizeof(Header) + header->json_len,
                       (size_t)header->data_len);
            }
            else
            {
                packet->binary_data = nullptr;
            }

            QueuePush(&conn->recv_queue, packet);
            memmove(frame_buf, frame_buf + total_size, (size_t)(frame_buf_offset - total_size));
            frame_buf_offset -= total_size;
        }
    }

    if (frame_buf)
        free(frame_buf);
    conn->state = CONN_DISCONNECTED;
}

// 创建连接
int CreateConnection(const char *ip, uint16_t port, bool is_server)
{
    std::unique_lock<std::mutex> lock(g_conn_mutex);

    if (!InitSockets())
    {
        return -1;
    }

    int slot = FindFreeSlot();
    if (slot < 0)
    {
        return -1;
    }

    Connection *conn = &g_connections[slot];
    memset(conn, 0, sizeof(Connection));

    conn->sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn->sock_fd == INVALID_SOCKET)
    {
        return -1;
    }

    int opt = 1;
    if (setsockopt(conn->sock_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) == SOCKET_ERROR)
    {
        CloseSocket(conn->sock_fd);
        return -1;
    }

    conn->addr.sin_family = AF_INET;
    conn->addr.sin_port = htons(port);
    conn->addr.sin_addr.s_addr = inet_addr(ip);

    if (is_server)
    {
        if (bind(conn->sock_fd, (struct sockaddr *)&conn->addr, sizeof(conn->addr)) == SOCKET_ERROR)
        {
            CloseSocket(conn->sock_fd);
            return -1;
        }

        if (listen(conn->sock_fd, WIN_DEFAULT_BACKLOG) == SOCKET_ERROR)
        {
            CloseSocket(conn->sock_fd);
            return -1;
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_fd = accept(conn->sock_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET)
        {
            CloseSocket(conn->sock_fd);
            return -1;
        }

        CloseSocket(conn->sock_fd);
        conn->sock_fd = client_fd;
    }
    else
    {
        if (connect(conn->sock_fd, (struct sockaddr *)&conn->addr, sizeof(conn->addr)) == SOCKET_ERROR)
        {
            CloseSocket(conn->sock_fd);
            return -1;
        }
    }

    QueueInit(&conn->send_queue);
    QueueInit(&conn->recv_queue);

    conn->state = CONN_CONNECTED;
    conn->threads_running = true;

    int slot_arg = slot;
    conn->send_thread = std::thread(SendThreadFunc, slot_arg);
    conn->recv_thread = std::thread(RecvThreadFunc, slot_arg);

    return slot;
}

// 发送数据
int SendData(int socket_id, const char *jsontext, const char *data, size_t length)
{
    if (socket_id < 0 || socket_id >= MAX_CONNECTIONS)
    {
        return -1;
    }

    std::unique_lock<std::mutex> lock(g_conn_mutex);
    Connection *conn = &g_connections[socket_id];

    if (conn->state != CONN_CONNECTED)
    {
        return -1;
    }

    // 创建数据包
    Packet *packet = (Packet *)malloc(sizeof(Packet));
    if (!packet)
    {
        return -1;
    }
    memset(packet, 0, sizeof(Packet));

    // 填充帧头
    packet->header.magic = 0xDEADBEEF;
    packet->header.data_len = length;

    // 复制JSON数据
    if (jsontext)
    {
        packet->header.json_len = (uint32_t)(strlen(jsontext) + 1);
        packet->json_str = (char *)malloc(packet->header.json_len);
        if (!packet->json_str)
        {
            free(packet);
            return -1;
        }
        strcpy(packet->json_str, jsontext);
    }
    else
    {
        packet->header.json_len = 0;
        packet->json_str = nullptr;
    }

    // 复制二进制数据
    if (data && length > 0)
    {
        packet->binary_data = (char *)malloc(length);
        if (!packet->binary_data)
        {
            if (packet->json_str)
            {
                free(packet->json_str);
            }
            free(packet);
            return -1;
        }
        memcpy(packet->binary_data, data, length);
    }
    else
    {
        packet->binary_data = nullptr;
    }

    packet->data_len = length;

    // 入队发送队列
    QueuePush(&conn->send_queue, packet);

    return 0;
}

// 接收数据
int RecvData(int socket_id, char **jsontext, char **data, size_t *out_length, int timeout_ms)
{
    if (socket_id < 0 || socket_id >= MAX_CONNECTIONS || !jsontext || !data || !out_length)
    {
        return -1;
    }

    *jsontext = nullptr;
    *data = nullptr;
    *out_length = 0;

    std::unique_lock<std::mutex> lock(g_conn_mutex);
    Connection *conn = &g_connections[socket_id];

    if (conn->state != CONN_CONNECTED && conn->state != CONN_DISCONNECTED)
    {
        return -1;
    }

    lock.unlock();

    // 从接收队列获取数据包
    Packet *packet = QueuePop(&conn->recv_queue, timeout_ms);

    if (!packet)
    {
        // 检查是超时还是错误
        if (conn->state == CONN_DISCONNECTED)
        {
            return -1;
        }
        return -2; // 超时
    }

    // 返回JSON数据
    if (packet->json_str)
    {
        *jsontext = packet->json_str;
    }
    else
    {
        *jsontext = nullptr;
    }

    // 返回二进制数据
    if (packet->binary_data && packet->data_len > 0)
    {
        *data = packet->binary_data;
        *out_length = packet->data_len;
    }
    else
    {
        *data = nullptr;
        *out_length = 0;
    }

    free(packet);

    return 0;
}

// 关闭连接
void CloseConnection(int socket_id)
{
    if (socket_id < 0 || socket_id >= MAX_CONNECTIONS)
    {
        return;
    }

    std::unique_lock<std::mutex> lock(g_conn_mutex);
    Connection *conn = &g_connections[socket_id];

    if (conn->state == CONN_IDLE)
    {
        return;
    }

    // 停止线程
    conn->threads_running = false;

    // 关闭套接字
    if (conn->sock_fd != INVALID_SOCKET)
    {
        CloseSocket(conn->sock_fd);
        conn->sock_fd = INVALID_SOCKET;
    }

    // 清空队列
    QueueClear(&conn->send_queue);
    QueueClear(&conn->recv_queue);

    // 等待线程结束
    lock.unlock();

    if (conn->send_thread.joinable())
    {
        conn->send_thread.join();
    }
    if (conn->recv_thread.joinable())
    {
        conn->recv_thread.join();
    }

    lock.lock();
    conn->state = CONN_IDLE;
}