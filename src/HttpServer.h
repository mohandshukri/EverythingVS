#pragma once
#include "IndexEngine.h"
#include <atomic>
#include <thread>

class HttpServer {
public:
    explicit HttpServer(IndexEngine& engine);
    ~HttpServer();

    bool Start(int port);
    void Stop();
    bool IsRunning() const { return m_running.load(); }
    int  Port() const { return m_port; }

private:
    void ServerLoop();
    void HandleClient(uintptr_t clientSocket);

    IndexEngine&      m_engine;
    std::atomic<bool> m_running{false};
    uintptr_t         m_listenSock = ~static_cast<uintptr_t>(0);
    int               m_port = 8080;
    std::thread       m_thread;
};
