#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <functional>

namespace deptran {

/**
 * ConcurrentMap - Thread-safe hash map with reader-writer locking
 * 
 * Allows multiple concurrent readers but exclusive writers.
 * Uses std::shared_mutex for efficient read-heavy workloads.
 * 
 * Template parameters:
 * @tparam K Key type (must be hashable)
 * @tparam V Value type
 * @tparam Hash Hash function (default: std::hash<K>)
 * @tparam Equal Equality function (default: std::equal_to<K>)
 */
template<typename K, 
         typename V, 
         typename Hash = std::hash<K>,
         typename Equal = std::equal_to<K>>
class ConcurrentMap {
public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<const K, V>;

    /**
     * Default constructor
     */
    ConcurrentMap() = default;

    /**
     * Insert or update a key-value pair
     * @param key Key to insert/update
     * @param value Value to store
     * @return true if inserted (new key), false if updated (existing key)
     */
    bool insert(const K& key, const V& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto result = map_.insert({key, value});
        if (!result.second) {
            // Key exists, update value
            result.first->second = value;
        }
        return result.second;
    }

    /**
     * Insert only if key doesn't exist
     * @param key Key to insert
     * @param value Value to store
     * @return true if inserted, false if key already exists
     */
    bool insert_if_absent(const K& key, const V& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return map_.insert({key, value}).second;
    }

    /**
     * Get value associated with key
     * @param key Key to lookup
     * @param out_value Output parameter for value
     * @return true if key found, false otherwise
     */
    bool get(const K& key, V& out_value) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            out_value = it->second;
            return true;
        }
        return false;
    }

    /**
     * Get value with optional return type
     * @param key Key to lookup
     * @return std::optional containing value if found, std::nullopt otherwise
     */
    std::optional<V> get(const K& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * Check if key exists
     * @param key Key to check
     * @return true if key exists
     */
    bool contains(const K& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }

    /**
     * Erase a key-value pair
     * @param key Key to erase
     * @return true if key was found and erased, false otherwise
     */
    bool erase(const K& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return map_.erase(key) > 0;
    }

    /**
     * Update value if key exists
     * @param key Key to update
     * @param updater Function that takes old value and returns new value
     * @return true if key was found and updated, false otherwise
     */
    bool update(const K& key, std::function<V(const V&)> updater) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second = updater(it->second);
            return true;
        }
        return false;
    }

    /**
     * Insert or update using a function
     * @param key Key to insert/update
     * @param on_insert Function to generate value if key doesn't exist
     * @param on_update Function to update value if key exists
     */
    void upsert(const K& key, 
                std::function<V()> on_insert,
                std::function<V(const V&)> on_update) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second = on_update(it->second);
        } else {
            map_[key] = on_insert();
        }
    }

    /**
     * Get size of map
     * @return Number of key-value pairs
     */
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.size();
    }

    /**
     * Check if map is empty
     * @return true if map contains no elements
     */
    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.empty();
    }

    /**
     * Clear all entries
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_.clear();
    }

    /**
     * Apply a function to each element (read-only)
     * @param func Function to apply to each key-value pair
     */
    void for_each(std::function<void(const K&, const V&)> func) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& pair : map_) {
            func(pair.first, pair.second);
        }
    }

    /**
     * Get all keys
     * @return Vector of all keys
     */
    std::vector<K> keys() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<K> result;
        result.reserve(map_.size());
        for (const auto& pair : map_) {
            result.push_back(pair.first);
        }
        return result;
    }

    /**
     * Get all values
     * @return Vector of all values
     */
    std::vector<V> values() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<V> result;
        result.reserve(map_.size());
        for (const auto& pair : map_) {
            result.push_back(pair.second);
        }
        return result;
    }

    /**
     * Get snapshot of all entries
     * @return Vector of key-value pairs
     */
    std::vector<std::pair<K, V>> snapshot() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::pair<K, V>> result;
        result.reserve(map_.size());
        for (const auto& pair : map_) {
            result.push_back(pair);
        }
        return result;
    }

private:
    std::unordered_map<K, V, Hash, Equal> map_;
    mutable std::shared_mutex mutex_;
};

} // namespace deptran
