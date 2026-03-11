#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>
#include <type_traits>

namespace lom {

template <typename T, std::size_t Capacity>
class LockFreeMPSCQueue {
    static_assert(Capacity > 1, "Capacity must be > 1");
    static_assert(std::is_move_constructible_v<T>, "T must be move-constructible");
    static_assert(std::is_move_assignable_v<T>, "T must be move-assignable");

public:
    LockFreeMPSCQueue() : head_(0), tail_(0) {
        for (auto& slot : slots_) {
            slot.ready.store(false, std::memory_order_relaxed);
        }
    }

    bool try_push(const T& value) {
        return emplace_impl(value);
    }

    bool try_push(T&& value) {
        return emplace_impl(std::move(value));
    }

    bool try_pop(T& out) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        Slot& slot = slots_[head % Capacity];
        if (!slot.ready.load(std::memory_order_acquire)) {
            return false;
        }

        out = std::move(slot.value);
        slot.ready.store(false, std::memory_order_release);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size_approx() const {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return tail >= head ? (tail - head) : 0;
    }

private:
    struct Slot {
        std::atomic<bool> ready{false};
        T value{};
    };

    template <typename U>
    bool emplace_impl(U&& value) {
        while (true) {
            std::size_t tail = tail_.load(std::memory_order_relaxed);
            const std::size_t head = head_.load(std::memory_order_acquire);
            if (tail - head >= Capacity) {
                return false;
            }

            if (!tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                std::this_thread::yield();
                continue;
            }

            Slot& slot = slots_[tail % Capacity];
            while (slot.ready.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            slot.value = std::forward<U>(value);
            slot.ready.store(true, std::memory_order_release);
            return true;
        }
    }

    std::array<Slot, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
};

} // namespace lom

