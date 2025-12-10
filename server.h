#pragma once
#include <string>
#include <filesystem>
#include "threadpool.h"
#include "router.h"

class HttpServer {
public:
    HttpServer(int port, int n_threads, const std::filesystem::path &root_dir);

    void run();

private:
    int port_;
    int n_threads_;
    std::filesystem::path root_dir_;
    ThreadPool pool_;
    LRUCache cache_;
    Router router_;

    int create_listen_socket();
    void handle_client(int client_sock, const std::string &client_ip);
};
