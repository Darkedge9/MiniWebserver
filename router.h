#pragma once
#include <string>
#include <filesystem>
#include "cache.h"
#include "http_parser.h"

class Router {
public:
    Router(const std::filesystem::path &root_dir, LRUCache &cache);

    // Returns HTTP response body; sets status_code and content_type.
    std::string handle(const HttpRequest &req, int &status_code, std::string &content_type);

private:
    std::filesystem::path root_;
    LRUCache &cache_;

    std::string serve_file(const std::filesystem::path &rel_path,
                           int &status_code,
                           std::string &content_type);
};
