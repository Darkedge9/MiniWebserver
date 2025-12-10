#include "http_parser.h"

#include <sstream>
#include <algorithm>
#include <cctype>

static std::string trim(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

bool parse_http_request(const std::string &raw, HttpRequest &out) {
    std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;

    std::istringstream iss(raw.substr(0, header_end));
    std::string line;

    // Request line
    if (!std::getline(iss, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream start_line(line);
    if (!(start_line >> out.method >> out.path >> out.version)) return false;

    // Headers
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        out.headers[key] = value;
    }

    return true;
}
