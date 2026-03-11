#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace lom {

template <typename T>
class LockFreeObjectPool {
public:
    explicit LockFreeObjectPool(std::size_t capacity) : nodes_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("Object pool capacity must be > 0");
        }
        for (std::size_t i = 0; i + 1 < nodes_.size(); ++i) {
            nodes_[i].next.store(static_cast<int32_t>(i + 1), std::memory_order_relaxed);
        }
        nodes_.back().next.store(k_null, std::memory_order_relaxed);
        head_tagged_.store(pack(0, 0), std::memory_order_relaxed);
    }

    T* acquire() {
        while (true) {
            const uint64_t tagged = head_tagged_.load(std::memory_order_acquire);
            const uint32_t head_idx = unpack_index(tagged);
            if (head_idx == k_null_u32) {
                return nullptr;
            }

            Node& node = nodes_[head_idx];
            const uint32_t next_idx = static_cast<uint32_t>(node.next.load(std::memory_order_relaxed));
            const uint64_t desired = pack(next_idx, unpack_tag(tagged) + 1);
            uint64_t expected = tagged;
            if (head_tagged_.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return &node.value;
            }
        }
    }

    void release(T* object) {
        if (object == nullptr) {
            return;
        }
        Node* node = node_from_object(object);
        const uint32_t idx = static_cast<uint32_t>(node - nodes_.data());

        while (true) {
            const uint64_t tagged = head_tagged_.load(std::memory_order_acquire);
            const uint32_t head_idx = unpack_index(tagged);
            node->next.store(static_cast<int32_t>(head_idx), std::memory_order_relaxed);
            const uint64_t desired = pack(idx, unpack_tag(tagged) + 1);
            uint64_t expected = tagged;
            if (head_tagged_.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    [[nodiscard]] std::size_t capacity() const {
        return nodes_.size();
    }

private:
    struct Node {
        std::atomic<int32_t> next{k_null};
        T value{};
    };

    static constexpr int32_t k_null = -1;
    static constexpr uint32_t k_null_u32 = 0xFFFFFFFFu;

    static uint64_t pack(uint32_t index, uint32_t tag) {
        return (static_cast<uint64_t>(tag) << 32) | static_cast<uint64_t>(index);
    }

    static uint32_t unpack_index(uint64_t tagged) {
        return static_cast<uint32_t>(tagged & 0xFFFFFFFFu);
    }

    static uint32_t unpack_tag(uint64_t tagged) {
        return static_cast<uint32_t>(tagged >> 32);
    }

    Node* node_from_object(T* object) {
        const auto base = reinterpret_cast<char*>(object) - offsetof(Node, value);
        return reinterpret_cast<Node*>(base);
    }

    std::vector<Node> nodes_;
    alignas(64) std::atomic<uint64_t> head_tagged_{0};
};

} // namespace lom
