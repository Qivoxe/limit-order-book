
#include "lob/order_book.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    constexpr std::uint64_t order_count = 500'000;

    lob::OrderBook book;
    std::vector<lob::OrderId> ids;
    ids.reserve(order_count);

    std::cout << "Cancellation Benchmark\n";
    std::cout << "----------------------\n";
    std::cout << "Resting orders: " << order_count << '\n';

    // Populate one price level with many orders.
    for (std::uint64_t i = 1; i <= order_count; ++i) {
        const lob::Order order{
            i,
            lob::Side::Buy,
            100,
            10
        };

        if (!book.add_order(order)) {
            std::cerr << "Failed to add order " << i << '\n';
            return 1;
        }

        ids.push_back(i);
    }

    // Cancel in insertion order, exercising removal from
    // the front, middle, and end of the price-level queue.
    const auto start = std::chrono::steady_clock::now();

    for (const auto id : ids) {
        if (!book.cancel_order(id)) {
            std::cerr << "Failed to cancel order " << id << '\n';
            return 1;
        }
    }

    const auto stop = std::chrono::steady_clock::now();

    const double seconds =
        std::chrono::duration<double>(stop - start).count();

    const double cancellations_per_second =
        static_cast<double>(order_count) / seconds;

    std::cout << "Cancelled: " << order_count << '\n';
    std::cout << "Elapsed: " << seconds << " seconds\n";
    std::cout << "Cancellation throughput: "
              << cancellations_per_second
              << " cancellations/second\n";
    std::cout << "Remaining orders: " << book.order_count() << '\n';

    if (book.order_count() != 0 || book.best_bid().has_value()) {
        std::cerr << "Book invariant check failed\n";
        return 1;
    }

    std::cout << "Final book state: empty\n";
    return 0;
}
