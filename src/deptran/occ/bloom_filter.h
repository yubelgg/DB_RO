#pragma once

#include <vector>
#include <functional>
#include <cstdint>
#include <cmath>
#include <atomic>

namespace janus {

/**
 * BloomFilter - Probabilistic set membership testing
 * 
 * Properties:
 * - Can tell you "definitely not in set" or "maybe in set"
 * - False positives possible, false negatives impossible
 * - Memory efficient (uses bits, not full objects)
 * - Fast O(k) operations where k = number of hash functions
 * 
 * Use cases:
 * - Quick conflict pre-filtering before expensive checks
 * - Reducing lock contention by filtering out non-conflicts
 * - Cache-friendly membership testing
 * 
 * Thread-safety: Thread-safe for concurrent reads/writes
 * 
 * Example:
 *   BloomFilter<int> filter(10000, 0.01); // 10k elements, 1% false positive rate
 *   filter.Add(42);
 *   if (filter.MayContain(42)) {
 *     // Do expensive exact check
 *   }
 */
template <typename T>
class BloomFilter {
public:
  /**
   * Constructor
   * 
   * @param expected_elements - Expected number of elements to insert
   * @param false_positive_rate - Desired false positive rate (e.g., 0.01 for 1%)
   */
  BloomFilter(size_t expected_elements, double false_positive_rate = 0.01)
      : num_elements_(0) {
    
    // Calculate optimal number of bits
    // m = -n * ln(p) / (ln(2)^2)
    // where n = expected elements, p = false positive rate
    double m = -1.0 * expected_elements * std::log(false_positive_rate) 
               / (std::log(2.0) * std::log(2.0));
    num_bits_ = static_cast<size_t>(std::ceil(m));
    
    // Calculate optimal number of hash functions
    // k = (m/n) * ln(2)
    double k = (static_cast<double>(num_bits_) / expected_elements) * std::log(2.0);
    num_hashes_ = static_cast<size_t>(std::ceil(k));
    
    // Ensure at least 1 hash function
    if (num_hashes_ == 0) num_hashes_ = 1;
    
    // Initialize bit array (use bytes, access individual bits)
    size_t num_bytes = (num_bits_ + 7) / 8;
    bits_.resize(num_bytes, 0);
  }
  
  /**
   * Constructor with explicit bit count and hash count
   * 
   * @param num_bits - Number of bits in the filter
   * @param num_hashes - Number of hash functions to use
   */
  BloomFilter(size_t num_bits, size_t num_hashes)
      : num_bits_(num_bits), 
        num_hashes_(num_hashes),
        num_elements_(0) {
    
    if (num_hashes_ == 0) num_hashes_ = 1;
    
    size_t num_bytes = (num_bits_ + 7) / 8;
    bits_.resize(num_bytes, 0);
  }
  
  /**
   * Add an element to the filter
   */
  void Add(const T& element) {
    for (size_t i = 0; i < num_hashes_; i++) {
      size_t bit_index = Hash(element, i) % num_bits_;
      SetBit(bit_index);
    }
    num_elements_.fetch_add(1, std::memory_order_relaxed);
  }
  
  /**
   * Check if element may be in the set
   * Returns: true if element might be in set (or false positive)
   *          false if element is definitely not in set
   */
  bool MayContain(const T& element) const {
    for (size_t i = 0; i < num_hashes_; i++) {
      size_t bit_index = Hash(element, i) % num_bits_;
      if (!GetBit(bit_index)) {
        return false; // Definitely not in set
      }
    }
    return true; // Maybe in set
  }
  
  /**
   * Clear all elements from the filter
   */
  void Clear() {
    for (auto& byte : bits_) {
      byte.store(0, std::memory_order_relaxed);
    }
    num_elements_.store(0, std::memory_order_relaxed);
  }
  
  /**
   * Get number of elements added (approximate, not atomic)
   */
  size_t NumElements() const {
    return num_elements_.load(std::memory_order_relaxed);
  }
  
  /**
   * Get number of bits in the filter
   */
  size_t NumBits() const {
    return num_bits_;
  }
  
  /**
   * Get number of hash functions used
   */
  size_t NumHashes() const {
    return num_hashes_;
  }
  
  /**
   * Estimate current false positive rate based on fill ratio
   * Returns: Estimated probability of false positive
   */
  double EstimatedFalsePositiveRate() const {
    size_t n = num_elements_.load(std::memory_order_relaxed);
    if (n == 0) return 0.0;
    
    // p = (1 - e^(-kn/m))^k
    // where k = num_hashes, n = num_elements, m = num_bits
    double exponent = -1.0 * num_hashes_ * n / static_cast<double>(num_bits_);
    double base = 1.0 - std::exp(exponent);
    return std::pow(base, static_cast<double>(num_hashes_));
  }
  
  /**
   * Get memory usage in bytes
   */
  size_t MemoryUsage() const {
    return bits_.size() * sizeof(std::atomic<uint8_t>);
  }

private:
  size_t num_bits_;                          // Total number of bits
  size_t num_hashes_;                        // Number of hash functions
  std::atomic<size_t> num_elements_;         // Number of elements added
  std::vector<std::atomic<uint8_t>> bits_;   // Bit array (thread-safe)
  
  /**
   * Hash function combining element hash with seed
   * Uses double hashing: h_i(x) = h1(x) + i * h2(x)
   */
  size_t Hash(const T& element, size_t seed) const {
    std::hash<T> hasher;
    size_t h1 = hasher(element);
    size_t h2 = hasher(element) * 2654435761U; // Use different multiplier for h2
    
    return h1 + seed * h2;
  }
  
  /**
   * Set a bit at given index (thread-safe)
   */
  void SetBit(size_t bit_index) {
    size_t byte_index = bit_index / 8;
    uint8_t bit_offset = bit_index % 8;
    
    // Atomic OR operation to set the bit
    uint8_t mask = 1 << bit_offset;
    uint8_t old_val = bits_[byte_index].load(std::memory_order_relaxed);
    while (!bits_[byte_index].compare_exchange_weak(
        old_val, old_val | mask, 
        std::memory_order_relaxed, 
        std::memory_order_relaxed)) {
      // Retry if CAS failed
    }
  }
  
  /**
   * Get a bit at given index (thread-safe)
   */
  bool GetBit(size_t bit_index) const {
    size_t byte_index = bit_index / 8;
    uint8_t bit_offset = bit_index % 8;
    
    uint8_t byte_val = bits_[byte_index].load(std::memory_order_relaxed);
    return (byte_val & (1 << bit_offset)) != 0;
  }
};

/**
 * Specialization helpers for common pointer types
 */
template <typename T>
struct PointerHasher {
  size_t operator()(T* ptr) const {
    return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(ptr));
  }
};

/**
 * BloomFilter for Row pointers (common use case)
 */
class Row; // Forward declaration
using RowBloomFilter = BloomFilter<Row*>;

} // namespace janus
