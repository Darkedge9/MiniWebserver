#pragma once
#include <unordered_map>
#include <list>
#include <string>
#include <mutex>

class LRUCache {
public:
    explicit LRUCache(std::size_t capacity = 50);

    bool get(const std::string &key, std::string &value);
    void put(const std::string &key, const std::string &value);

private:
    std::size_t capacity_;
    std::list<std::string> order_;
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> map_;
    std::mutex mutex_;
};
