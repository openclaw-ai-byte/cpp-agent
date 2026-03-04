#pragma once

#include <memory>

namespace agent {

class Agent;

class HttpServer {
public:
    HttpServer(std::shared_ptr<Agent> agent, int port = 8080);
    ~HttpServer();
    
    void start();
    void stop();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agent
