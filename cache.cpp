#include "cache.h"

LRUCache::LRUCache(std::size_t capacity) : capacity_(capacity) {}

bool LRUCache::get(const std::string &key, std::string &value) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    // Move key to front
    order_.splice(order_.begin(), order_, it->second.second);
    value = it->second.first;
    return true;
}

void LRUCache::put(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second.first = value;
        order_.splice(order_.begin(), order_, it->second.second);
        return;
    }
    if (map_.size() >= capacity_) {
        const std::string &last = order_.back();
        map_.erase(last);
        order_.pop_back();
    }
    order_.push_front(key);
    map_[key] = {value, order_.begin()};
}
