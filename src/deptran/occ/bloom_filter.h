#pragma once

#include <vector>
#include <functional>
#include <cmath>
#include <cstdint>
#include <bit>
#include <string>

namespace deptran {

/**
 * BloomFilter - Probabilistic data structure for set membership testing
 * 
 * Space-efficient structure that can test whether an element is in a set.
 * - False positives are possible (says element is in set when it's not)
 * - False negatives are NOT possible (if it says no, element is definitely not in set)
 * 
 * Used in OCC for fast conflict detection between transaction read/write sets.
 * 
 * Template parameters:
 * @tparam T Type of elements to store (must be hashable)
 */
template<typename T>
class BloomFilter {
public:
    /**
     * Constructor
     * @param expected_elements Expected number of elements to insert
     * @param false_positive_rate Desired false positive probability (default 0.01 = 1%)
     */
    explicit BloomFilter(size_t expected_elements = 1000, 
                        double false_positive_rate = 0.01)
        : num_elements_(0) {
        
        // Calculate optimal bit array size and number of hash functions
        // m = -(n * ln(p)) / (ln(2)^2)
        // k = (m/n) * ln(2)
        
        if (expected_elements == 0) expected_elements = 1;
        if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
            false_positive_rate = 0.01;
        }
        
        double m = -(static_cast<double>(expected_elements) * std::log(false_positive_rate)) 
                   / (std::log(2.0) * std::log(2.0));
        
        bit_array_size_ = static_cast<size_t>(std::ceil(m));
        
        // Ensure bit_array_size_ is at least 64 and multiple of 64 for efficiency
        bit_array_size_ = ((bit_array_size_ + 63) / 64) * 64;
        
        num_hash_functions_ = static_cast<size_t>(
            std::ceil((static_cast<double>(bit_array_size_) / expected_elements) * std::log(2.0))
        );
        
        if (num_hash_functions_ == 0) num_hash_functions_ = 1;
        if (num_hash_functions_ > 10) num_hash_functions_ = 10; // Practical limit
        
        // Initialize bit array (using uint64_t for efficiency)
        size_t num_words = (bit_array_size_ + 63) / 64;
        bit_array_.resize(num_words, 0);
    }

    /**
     * Insert an element into the bloom filter
     * @param item Element to insert
     */
    void insert(const T& item) {
        uint64_t hash1 = std::hash<T>{}(item);
        uint64_t hash2 = hash1 * 0x9e3779b97f4a7c15ULL; // Golden ratio for second hash
        
        for (size_t i = 0; i < num_hash_functions_; ++i) {
            uint64_t hash = hash1 + i * hash2;
            size_t bit_index = hash % bit_array_size_;
            set_bit(bit_index);
        }
        
        ++num_elements_;
    }

    /**
     * Check if an element might be in the set
     * @param item Element to check
     * @return true if element might be in set (could be false positive),
     *         false if element is definitely not in set
     */
    bool contains(const T& item) const {
        uint64_t hash1 = std::hash<T>{}(item);
        uint64_t hash2 = hash1 * 0x9e3779b97f4a7c15ULL;
        
        for (size_t i = 0; i < num_hash_functions_; ++i) {
            uint64_t hash = hash1 + i * hash2;
            size_t bit_index = hash % bit_array_size_;
            if (!get_bit(bit_index)) {
                return false; // Definitely not in set
            }
        }
        
        return true; // Probably in set
    }

    /**
     * Clear all elements from the filter
     */
    void clear() {
        std::fill(bit_array_.begin(), bit_array_.end(), 0);
        num_elements_ = 0;
    }

    /**
     * Get the current false positive rate based on actual insertions
     * @return Estimated false positive probability
     */
    double estimated_fpp() const {
        if (num_elements_ == 0) return 0.0;
        
        // FPP = (1 - e^(-k*n/m))^k
        double exponent = -static_cast<double>(num_hash_functions_ * num_elements_) 
                         / bit_array_size_;
        return std::pow(1.0 - std::exp(exponent), num_hash_functions_);
    }

    /**
     * Get number of elements inserted (approximate)
     */
    size_t size() const { return num_elements_; }

    /**
     * Get capacity (bit array size)
     */
    size_t capacity() const { return bit_array_size_; }

    /**
     * Check if filter is empty
     */
    bool empty() const { return num_elements_ == 0; }

private:
    std::vector<uint64_t> bit_array_;  // Bit array stored as 64-bit words
    size_t bit_array_size_;            // Number of bits
    size_t num_hash_functions_;        // Number of hash functions (k)
    size_t num_elements_;              // Number of elements inserted

    /**
     * Set a bit at the given index
     */
    void set_bit(size_t index) {
        size_t word_index = index / 64;
        size_t bit_offset = index % 64;
        bit_array_[word_index] |= (1ULL << bit_offset);
    }

    /**
     * Get a bit at the given index
     */
    bool get_bit(size_t index) const {
        size_t word_index = index / 64;
        size_t bit_offset = index % 64;
        return (bit_array_[word_index] & (1ULL << bit_offset)) != 0;
    }
};

/**
 * Specialization for common OCC key types
 */
using KeyBloomFilter = BloomFilter<uint64_t>;
using StringBloomFilter = BloomFilter<std::string>;

} // namespace deptran
