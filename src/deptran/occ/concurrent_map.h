#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <memory>

namespace janus {

/**
 * ConcurrentMap - Thread-safe hash map using sharding
 * 
 * Uses 256 shards, each with its own lock, to reduce contention.
 * Much better than a single global lock for concurrent access.
 * 
 * Template parameters:
 *   K - Key type (must be hashable)
 *   V - Value type
 * 
 * Example:
 *   ConcurrentMap<int, std::string> map;
 *   map.Insert(42, "hello");
 *   auto val = map.TryGet(42);
 *   if (val) {
 *     std::cout << *val << std::endl;
 *   }
 */
template <typename K, typename V>
class ConcurrentMap {
public:
  static constexpr size_t NUM_SHARDS = 256;
  
  ConcurrentMap() {
    shards_.resize(NUM_SHARDS);
    mutexes_ = std::unique_ptr<std::mutex[]>(new std::mutex[NUM_SHARDS]);
  }
  
  ~ConcurrentMap() = default;
  
  // Non-copyable, non-movable
  ConcurrentMap(const ConcurrentMap&) = delete;
  ConcurrentMap& operator=(const ConcurrentMap&) = delete;
  ConcurrentMap(ConcurrentMap&&) = delete;
  ConcurrentMap& operator=(ConcurrentMap&&) = delete;
  
  /**
   * Insert or update a key-value pair
   * Returns: true if new key inserted, false if existing key updated
   */
  bool Insert(const K& key, const V& value) {
    size_t shard = GetShardIndex(key);
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    
    auto it = shards_[shard].find(key);
    bool is_new = (it == shards_[shard].end());
    shards_[shard][key] = value;
    
    return is_new;
  }
  
  /**
   * Try to get a value by key
   * Returns: std::optional with value if found, std::nullopt otherwise
   */
  std::optional<V> TryGet(const K& key) const {
    size_t shard = GetShardIndex(key);
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    
    auto it = shards_[shard].find(key);
    if (it != shards_[shard].end()) {
      return it->second;
    }
    return std::nullopt;
  }
  
  /**
   * Get a value by key, with default fallback
   * Returns: value if found, default_value otherwise
   */
  V GetOrDefault(const K& key, const V& default_value) const {
    auto result = TryGet(key);
    return result ? *result : default_value;
  }
  
  /**
   * Check if key exists
   */
  bool Contains(const K& key) const {
    size_t shard = GetShardIndex(key);
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    
    return shards_[shard].find(key) != shards_[shard].end();
  }
  
  /**
   * Remove a key
   * Returns: true if key was found and removed, false otherwise
   */
  bool Remove(const K& key) {
    size_t shard = GetShardIndex(key);
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    
    auto it = shards_[shard].find(key);
    if (it != shards_[shard].end()) {
      shards_[shard].erase(it);
      return true;
    }
    return false;
  }
  
  /**
   * Apply a function to a key's value if it exists
   * Returns: true if key was found and function applied, false otherwise
   */
  bool Update(const K& key, std::function<void(V&)> update_fn) {
    size_t shard = GetShardIndex(key);
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    
    auto it = shards_[shard].find(key);
    if (it != shards_[shard].end()) {
      update_fn(it->second);
      return true;
    }
    return false;
  }
  
  /**
   * Insert if key doesn't exist, or update existing value
   * update_fn is called with current value if key exists
   * Returns: true if new key inserted, false if existing key updated
   */
  bool InsertOrUpdate(const K& key, const V& initial_value, 
                      std::function<void(V&)> update_fn) {
    size_t shard = GetShardIndex(key);
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    
    auto it = shards_[shard].find(key);
    if (it != shards_[shard].end()) {
      update_fn(it->second);
      return false;
    } else {
      shards_[shard][key] = initial_value;
      return true;
    }
  }
  
  /**
   * Clear all entries
   */
  void Clear() {
    for (size_t i = 0; i < NUM_SHARDS; i++) {
      std::lock_guard<std::mutex> lock(mutexes_[i]);
      shards_[i].clear();
    }
  }
  
  /**
   * Get approximate size (not atomic across shards)
   */
  size_t Size() const {
    size_t total = 0;
    for (size_t i = 0; i < NUM_SHARDS; i++) {
      std::lock_guard<std::mutex> lock(mutexes_[i]);
      total += shards_[i].size();
    }
    return total;
  }
  
  /**
   * Check if map is empty (not atomic across shards)
   */
  bool Empty() const {
    for (size_t i = 0; i < NUM_SHARDS; i++) {
      std::lock_guard<std::mutex> lock(mutexes_[i]);
      if (!shards_[i].empty()) {
        return false;
      }
    }
    return true;
  }
  
  /**
   * Apply a function to all key-value pairs
   * Warning: Locks all shards sequentially, can be slow
   */
  void ForEach(std::function<void(const K&, const V&)> fn) const {
    for (size_t i = 0; i < NUM_SHARDS; i++) {
      std::lock_guard<std::mutex> lock(mutexes_[i]);
      for (const auto& pair : shards_[i]) {
        fn(pair.first, pair.second);
      }
    }
  }
  
  /**
   * Get all keys (snapshot, not atomic)
   */
  std::vector<K> GetKeys() const {
    std::vector<K> keys;
    for (size_t i = 0; i < NUM_SHARDS; i++) {
      std::lock_guard<std::mutex> lock(mutexes_[i]);
      for (const auto& pair : shards_[i]) {
        keys.push_back(pair.first);
      }
    }
    return keys;
  }
  
  /**
   * Get all values (snapshot, not atomic)
   */
  std::vector<V> GetValues() const {
    std::vector<V> values;
    for (size_t i = 0; i < NUM_SHARDS; i++) {
      std::lock_guard<std::mutex> lock(mutexes_[i]);
      for (const auto& pair : shards_[i]) {
        values.push_back(pair.second);
      }
    }
    return values;
  }

private:
  // Sharded hash maps
  std::vector<std::unordered_map<K, V>> shards_;
  
  // Per-shard mutexes (mutable for const methods)
  mutable std::unique_ptr<std::mutex[]> mutexes_;
  
  /**
   * Get shard index for a key using hash function
   */
  size_t GetShardIndex(const K& key) const {
    std::hash<K> hasher;
    return hasher(key) % NUM_SHARDS;
  }
};

} // namespace janus
