#include "core/ai_cache.h"
#include <algorithm>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>

AICache::AICache(size_t max_size, std::chrono::seconds default_ttl)
    : max_size_(max_size), default_ttl_(default_ttl),
      last_cleanup_(std::chrono::steady_clock::now()) {}

void AICache::put(const std::string &key, const std::string &value,
                  std::chrono::seconds ttl) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (ttl.count() == 0) {
    ttl = default_ttl_;
  }

  auto now = std::chrono::steady_clock::now();

  // Check if key exists
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    // Update existing entry
    it->second.data = value;
    it->second.created_at = now;
    it->second.expiry = now + ttl;
    it->second.last_accessed = now;

    // Update access order
    access_order_.erase(access_map_[key]);
    access_order_.push_front(key);
    access_map_[key] = access_order_.begin();
    return;
  }

  // Evict if needed
  while (cache_.size() >= max_size_) {
    evictLRU();
  }

  // Add new entry
  CacheEntry entry;
  entry.data = value;
  entry.created_at = now;
  entry.expiry = now + ttl;
  entry.last_accessed = now;
  entry.access_count = 0;

  cache_[key] = entry;
  access_order_.push_front(key);
  access_map_[key] = access_order_.begin();

  // Periodic cleanup
  if (now - last_cleanup_ > CLEANUP_INTERVAL) {
    cleanupExpired();
    last_cleanup_ = now;
  }
}

std::optional<std::string> AICache::get(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = cache_.find(key);
  if (it == cache_.end()) {
    misses_++;
    return std::nullopt;
  }

  // Check if expired
  if (it->second.isExpired()) {
    cache_.erase(it);
    access_order_.erase(access_map_[key]);
    access_map_.erase(key);
    misses_++;
    return std::nullopt;
  }

  // Update access info
  it->second.last_accessed = std::chrono::steady_clock::now();
  it->second.access_count++;

  // Update access order
  access_order_.erase(access_map_[key]);
  access_order_.push_front(key);
  access_map_[key] = access_order_.begin();

  hits_++;
  return it->second.data;
}

void AICache::invalidate(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = cache_.find(key);
  if (it != cache_.end()) {
    cache_.erase(it);
    if (access_map_.count(key)) {
      access_order_.erase(access_map_[key]);
      access_map_.erase(key);
    }
  }
}

void AICache::invalidatePattern(const std::string &pattern) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto it = cache_.begin(); it != cache_.end();) {
    if (it->first.find(pattern) != std::string::npos) {
      if (access_map_.count(it->first)) {
        access_order_.erase(access_map_[it->first]);
        access_map_.erase(it->first);
      }
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
}

void AICache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  access_order_.clear();
  access_map_.clear();
}

size_t AICache::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.size();
}

AICache::Stats AICache::getStats() const {
  std::lock_guard<std::mutex> lock(mutex_);

  Stats stats;
  stats.entries = cache_.size();
  stats.max_size = max_size_;
  stats.hits = hits_.load();
  stats.misses = misses_.load();

  size_t total = stats.hits + stats.misses;
  stats.hit_rate = total > 0 ? static_cast<double>(stats.hits) / total : 0.0;

  return stats;
}

std::string AICache::generateKey(const std::string &image_data,
                                 const std::string &config) {
  // Generate SHA256 hash of image_data + config using EVP API (OpenSSL 3.0 compatible)
  std::string input = image_data + "|" + config;

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  if (mdctx == nullptr) {
    // Fallback: return a simple hash if EVP fails
    return std::to_string(std::hash<std::string>{}(input));
  }

  if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(mdctx, input.c_str(), input.length()) != 1 ||
      EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(mdctx);
    // Fallback: return a simple hash if EVP fails
    return std::to_string(std::hash<std::string>{}(input));
  }

  EVP_MD_CTX_free(mdctx);

  std::stringstream ss;
  for (unsigned int i = 0; i < hash_len; i++) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash[i]);
  }

  return ss.str();
}

void AICache::evictLRU() {
  if (access_order_.empty()) {
    return;
  }

  // Remove least recently used
  std::string lru_key = access_order_.back();
  access_order_.pop_back();
  cache_.erase(lru_key);
  access_map_.erase(lru_key);
}

void AICache::cleanupExpired() {
  auto now = std::chrono::steady_clock::now();

  for (auto it = cache_.begin(); it != cache_.end();) {
    if (it->second.isExpired()) {
      if (access_map_.count(it->first)) {
        access_order_.erase(access_map_[it->first]);
        access_map_.erase(it->first);
      }
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
}
