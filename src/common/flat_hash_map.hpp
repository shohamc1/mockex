#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

template <typename K, typename V, K EmptyKey = K{}>
class FlatHashMap {
public:
    explicit FlatHashMap(size_t capacity = 0) {
        size_t desired = capacity * 10 / 7;
        capacity_ = 1;
        while (capacity_ < desired) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        keys_.resize(capacity_);
        vals_.resize(capacity_);
        clear();
    }

    V* find(K key) {
        if (key == EmptyKey) return nullptr;
        size_t idx = hash(key);
        for (;;) {
            if (keys_[idx] == EmptyKey) return nullptr;
            if (keys_[idx] == key) return &vals_[idx];
            ++idx;
            idx &= mask_;
        }
    }

    const V* find(K key) const {
        if (key == EmptyKey) return nullptr;
        size_t idx = hash(key);
        for (;;) {
            if (keys_[idx] == EmptyKey) return nullptr;
            if (keys_[idx] == key) return &vals_[idx];
            ++idx;
            idx &= mask_;
        }
    }

    bool insert(K key, const V& value) {
        if (key == EmptyKey) return false;
        if (size_ * 10 >= capacity_ * 7) grow();

        size_t idx = hash(key);
        for (;;) {
            if (keys_[idx] == EmptyKey) {
                keys_[idx] = key;
                vals_[idx] = value;
                ++size_;
                return true;
            }
            if (keys_[idx] == key) {
                vals_[idx] = value;
                return true;
            }
            ++idx;
            idx &= mask_;
        }
    }

    bool erase(K key) {
        if (key == EmptyKey) return false;
        size_t idx = hash(key);
        for (;;) {
            if (keys_[idx] == EmptyKey) return false;
            if (keys_[idx] == key) {
                --size_;
                backward_shift(idx);
                return true;
            }
            ++idx;
            idx &= mask_;
        }
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    void clear() {
        for (auto& k : keys_) k = EmptyKey;
        size_ = 0;
    }

    template <typename Fn>
    void for_each(Fn&& fn) {
        for (size_t i = 0; i < capacity_; ++i) {
            if (keys_[i] != EmptyKey) {
                fn(keys_[i], vals_[i]);
            }
        }
    }

private:
    size_t hash(K key) const {
        uint64_t h = static_cast<uint64_t>(key) * 0x9e3779b97f4a7c15ULL;
        h ^= h >> 33;
        return static_cast<size_t>(h) & mask_;
    }

    void backward_shift(size_t erased_idx) {
        size_t idx = erased_idx;
        for (;;) {
            size_t next = (idx + 1) & mask_;
            if (keys_[next] == EmptyKey) {
                keys_[idx] = EmptyKey;
                return;
            }
            size_t desired = hash(keys_[next]);
            size_t dist_next = (next - desired + capacity_) & mask_;
            if (dist_next == 0) {
                keys_[idx] = EmptyKey;
                return;
            }
            keys_[idx] = keys_[next];
            vals_[idx] = vals_[next];
            idx = next;
        }
    }

    void grow() {
        capacity_ <<= 1;
        mask_ = capacity_ - 1;

        std::vector<K> old_keys = std::move(keys_);
        std::vector<V> old_vals = std::move(vals_);
        keys_.resize(capacity_);
        vals_.resize(capacity_);
        for (auto& k : keys_) k = EmptyKey;
        size_ = 0;

        for (size_t i = 0; i < old_keys.size(); ++i) {
            if (old_keys[i] != EmptyKey) {
                insert(old_keys[i], old_vals[i]);
            }
        }
    }

    size_t capacity_ = 0;
    size_t mask_ = 0;
    size_t size_ = 0;
    std::vector<K> keys_;
    std::vector<V> vals_;
};
