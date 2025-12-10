#include "server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <atomic>

using namespace std;
namespace fs = filesystem;

// ------------ request log structures (for dashboard) --------------
struct RequestLogEntry {
    string time;
    string client;
    string path;
    int status;
    size_t bytes;
};

static deque<RequestLogEntry> g_recent_logs;
static const size_t MAX_RECENT_LOGS = 100;
static mutex g_log_mutex;

// global metrics
static atomic<size_t> g_total_requests{0};
static atomic<size_t> g_status_200{0};
static atomic<size_t> g_status_400{0};
static atomic<size_t> g_status_403{0};
static atomic<size_t> g_status_404{0};
static atomic<size_t> g_status_405{0};
static atomic<size_t> g_status_500{0};
static atomic<size_t> g_bytes_sent{0};

static atomic<int> g_active_connections{0};
static atomic<int> g_peak_connections{0};

static chrono::steady_clock::time_point g_start_time;

// ---- logging helpers ----
static string now_str() {
    using namespace chrono;
    auto t = system_clock::now();
    time_t tt = system_clock::to_time_t(t);
    tm tm{};
    localtime_r(&tt, &tm);
    ostringstream oss;
    oss << put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// called for every *user* request (we skip internal /admin/metrics)
static void log_request(const string &client,
                        const string &path,
                        int status,
                        size_t bytes) {
    // don't count dashboard polling endpoint in stats/log table
    if (path == "/admin/metrics") {
        return;
    }

    string ts = now_str();

    {
        lock_guard<mutex> lk(g_log_mutex);
        g_recent_logs.push_back(RequestLogEntry{ts, client, path, status, bytes});
        if (g_recent_logs.size() > MAX_RECENT_LOGS) {
            g_recent_logs.pop_front();
        }
    }

    g_total_requests.fetch_add(1, memory_order_relaxed);
    g_bytes_sent.fetch_add(bytes, memory_order_relaxed);

    switch (status) {
        case 200: g_status_200.fetch_add(1, memory_order_relaxed); break;
        case 400: g_status_400.fetch_add(1, memory_order_relaxed); break;
        case 403: g_status_403.fetch_add(1, memory_order_relaxed); break;
        case 404: g_status_404.fetch_add(1, memory_order_relaxed); break;
        case 405: g_status_405.fetch_add(1, memory_order_relaxed); break;
        case 500: g_status_500.fetch_add(1, memory_order_relaxed); break;
        default: break;
    }

    cout << "[" << ts << "] "
         << client << " \"" << path << "\" "
         << status << " " << bytes << endl;
}

// RAII helper: tracks active_connections & peak_connections
struct ConnectionGuard {
    atomic<int> &active;
    ConnectionGuard(atomic<int> &a) : active(a) {
        int curr = active.fetch_add(1, memory_order_relaxed) + 1;
        int prev_peak = g_peak_connections.load(memory_order_relaxed);
        if (curr > prev_peak) {
            g_peak_connections.store(curr, memory_order_relaxed);
        }
    }
    ~ConnectionGuard() {
        active.fetch_sub(1, memory_order_relaxed);
    }
};

// Build JSON for /admin/metrics endpoint
static string build_metrics_json() {
    deque<RequestLogEntry> copy;
    {
        lock_guard<mutex> lk(g_log_mutex);
        copy = g_recent_logs; // copy so we don't hold lock while building JSON
    }

    using namespace chrono;
    auto now = steady_clock::now();
    auto uptime_sec = duration_cast<seconds>(now - g_start_time).count();
    if (uptime_sec < 0) uptime_sec = 0;

    size_t total   = g_total_requests.load(memory_order_relaxed);
    size_t bytes   = g_bytes_sent.load(memory_order_relaxed);
    int    active  = g_active_connections.load(memory_order_relaxed);
    int    peak    = g_peak_connections.load(memory_order_relaxed);

    size_t s200 = g_status_200.load(memory_order_relaxed);
    size_t s400 = g_status_400.load(memory_order_relaxed);
    size_t s403 = g_status_403.load(memory_order_relaxed);
    size_t s404 = g_status_404.load(memory_order_relaxed);
    size_t s405 = g_status_405.load(memory_order_relaxed);
    size_t s500 = g_status_500.load(memory_order_relaxed);

    ostringstream oss;
    oss << "{";
    oss << "\"total_requests\":" << total << ",";
    oss << "\"uptime_seconds\":" << uptime_sec << ",";
    oss << "\"active_connections\":" << active << ",";
    oss << "\"peak_connections\":" << peak << ",";
    oss << "\"bytes_sent\":" << bytes << ",";

    // status breakdown
    oss << "\"status_counts\":{";
    oss << "\"200\":" << s200 << ",";
    oss << "\"400\":" << s400 << ",";
    oss << "\"403\":" << s403 << ",";
    oss << "\"404\":" << s404 << ",";
    oss << "\"405\":" << s405 << ",";
    oss << "\"500\":" << s500;
    oss << "},";

    // recent logs
    oss << "\"recent\":[";
    bool first = true;
    for (const auto &e : copy) {
        if (!first) oss << ",";
        first = false;
        oss << "{";
        oss << "\"time\":\""   << e.time   << "\",";
        oss << "\"client\":\"" << e.client << "\",";
        oss << "\"path\":\""   << e.path   << "\",";
        oss << "\"status\":"   << e.status << ",";
        oss << "\"bytes\":"    << e.bytes;
        oss << "}";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

// ---- HttpServer implementation ----
HttpServer::HttpServer(int port, int n_threads, const fs::path &root_dir)
    : port_(port),
      n_threads_(n_threads > 0 ? n_threads : 1),
      root_dir_(root_dir),
      pool_(n_threads_),
      cache_(100),
      router_(root_dir_, cache_) {}

int HttpServer::create_listen_socket() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        ::close(fd);
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(fd);
        return -1;
    }

    if (listen(fd, 128) < 0) {
        perror("listen");
        ::close(fd);
        return -1;
    }

    return fd;
}

void HttpServer::handle_client(int client_sock, const string &client_ip) {
    ConnectionGuard guard(g_active_connections); // auto-inc/dec active connections

    constexpr size_t BUF_SIZE = 8192;
    string req;
    char buf[BUF_SIZE];

    ssize_t recvd;
    while ((recvd = ::recv(client_sock, buf, BUF_SIZE, 0)) > 0) {
        req.append(buf, buf + recvd);
        if (req.find("\r\n\r\n") != string::npos) break;
        if (req.size() > 64 * 1024) break; // very large header -> bail
    }

    if (recvd < 0) {
        ::close(client_sock);
        return;
    }

    HttpRequest http_req;
    if (!parse_http_request(req, http_req)) {
        string body = "Bad Request\n";
        ostringstream resp;
        resp << "HTTP/1.0 400 Bad Request\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Content-Type: text/plain\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        string out = resp.str();
        ::send(client_sock, out.c_str(), out.size(), 0);
        // treat as 400
        log_request(client_ip, "-", 400, out.size());
        ::close(client_sock);
        return;
    }

    // ---- Special endpoint: /admin/metrics ----
    if (http_req.path == "/admin/metrics") {
        string body = build_metrics_json();
        ostringstream resp;
        resp << "HTTP/1.0 200 OK\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Connection: close\r\n\r\n";

        string header = resp.str();
        ::send(client_sock, header.c_str(), header.size(), 0);
        if (!body.empty()) {
            ::send(client_sock, body.data(), body.size(), 0);
        }

        // we do NOT log /admin/metrics in log_request()
        log_request(client_ip, http_req.path, 200, header.size() + body.size());
        ::close(client_sock);
        return;
    }

    // ---- Optional simple health endpoint ----
    if (http_req.path == "/admin/health") {
        string body = "OK\n";
        ostringstream resp;
        resp << "HTTP/1.0 200 OK\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Content-Type: text/plain\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        string out = resp.str();
        ::send(client_sock, out.c_str(), out.size(), 0);
        log_request(client_ip, http_req.path, 200, out.size());
        ::close(client_sock);
        return;
    }

    int status = 0;
    string content_type;
    string body = router_.handle(http_req, status, content_type);

    string status_text;
    switch (status) {
        case 200: status_text = "OK"; break;
        case 400: status_text = "Bad Request"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        default:  status_text = "Unknown"; break;
    }

    ostringstream resp;
    resp << "HTTP/1.0 " << status << " " << status_text << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Connection: close\r\n\r\n";

    string header = resp.str();
    ::send(client_sock, header.c_str(), header.size(), 0);
    if (!body.empty()) {
        ::send(client_sock, body.data(), body.size(), 0);
    }

    log_request(client_ip, http_req.path, status, header.size() + body.size());
    ::close(client_sock);
}

void HttpServer::run() {
    if (!fs::exists(root_dir_)) {
        cerr << "Root directory does not exist: " << root_dir_ << endl;
        return;
    }

    int listen_fd = create_listen_socket();
    if (listen_fd < 0) return;

    cout << "MiniWebServer listening on 0.0.0.0:" << port_
         << "  threads=" << n_threads_
         << "  root=" << root_dir_ << endl;

    for (;;) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd,
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char ipbuf[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
        string client_ip(ipbuf);

        pool_.enqueue([this, client_fd, client_ip]() {
            this->handle_client(client_fd, client_ip);
        });
    }

    ::close(listen_fd);
}

// ---- main() entry point ----
int main(int argc, char** argv) {
    int port = 8080;
    int threads = thread::hardware_concurrency();
    string root = "./static";

    if (argc >= 2) port = atoi(argv[1]);
    if (argc >= 3) threads = atoi(argv[2]);
    if (argc >= 4) root = argv[3];

    // start uptime timer
    g_start_time = chrono::steady_clock::now();

    HttpServer server(port, threads, root);
    server.run();
    return 0;
}
