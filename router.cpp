#include "router.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string get_mime_type(const std::string &path) {
    fs::path p(path);
    auto ext = p.extension().string();
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

Router::Router(const fs::path &root_dir, LRUCache &cache)
    : root_(root_dir), cache_(cache) {}

std::string Router::handle(const HttpRequest &req, int &status_code, std::string &content_type) {
    if (req.method != "GET") {
        status_code = 405;
        content_type = "text/plain";
        return "Method Not Allowed\n";
    }

    std::string path = req.path.empty() ? "/" : req.path;
    if (path == "/") path = "/index.html";

    fs::path rel = fs::path(path).lexically_normal();
    std::string rel_str = rel.generic_string();
    if (rel_str.find("..") != std::string::npos) {
        status_code = 403;
        content_type = "text/plain";
        return "Forbidden\n";
    }

    return serve_file(rel, status_code, content_type);
}

std::string Router::serve_file(const fs::path &rel_path, int &status_code, std::string &content_type) {
    fs::path full = root_ / rel_path.relative_path();

    if (!fs::exists(full) || fs::is_directory(full)) {
        status_code = 404;
        content_type = "text/plain";
        return "Not Found\n";
    }

    std::string key = full.string();
    std::string data;
    if (!cache_.get(key, data)) {
        std::ifstream ifs(full, std::ios::binary);
        if (!ifs) {
            status_code = 500;
            content_type = "text/plain";
            return "Internal Server Error\n";
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        data = oss.str();
        cache_.put(key, data);
    }

    status_code = 200;
    content_type = get_mime_type(full.string());
    return data;
}
