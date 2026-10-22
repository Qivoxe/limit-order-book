#include "lob/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

void report(std::string_view name, std::uint64_t operations, Clock::time_point start,
            Clock::time_point stop)
{
    const double seconds = std::chrono::duration<double>(stop - start).count();
    std::cout << std::left << std::setw(27) << name
              << " operations=" << operations
              << " elapsed_s=" << std::fixed << std::setprecision(6) << seconds
              << " throughput_ops_s=" << std::setprecision(0)
              << static_cast<double>(operations) / seconds << '\n';
}

bool single_price(std::uint64_t pairs)
{
    lob::OrderBook book;
    lob::OrderId id = 1;
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < pairs; ++i)
    {
        book.submit_order({id++, lob::Side::Sell, 100, 5});
        const auto trades = book.submit_order({id++, lob::Side::Buy, 100, 5});
        if (trades.size() != 1 || trades.front().quantity != 5) return false;
    }
    const auto stop = Clock::now();
    report("single-price submits", pairs * 2, start, stop);
    return book.order_count() == 0;
}

bool event_enabled_matching(std::uint64_t pairs)
{
    lob::OrderBook book;
    lob::OrderId id = 1;
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < pairs; ++i)
    {
        const auto sell = book.submit_order_with_events({id++, lob::Side::Sell, 100, 5});
        const auto buy = book.submit_order_with_events({id++, lob::Side::Buy, 100, 5});
        if (!sell.accepted || !buy.accepted || buy.trades.size() != 1 || buy.events.empty())
            return false;
    }
    const auto stop = Clock::now();
    report("event-enabled submits", pairs * 2, start, stop);
    return book.order_count() == 0;
}

bool multiple_levels(std::uint64_t batches)
{
    constexpr std::uint64_t levels = 32;
    lob::OrderBook book;
    lob::OrderId id = 1;
    const auto start = Clock::now();
    for (std::uint64_t batch = 0; batch < batches; ++batch)
    {
        for (std::uint64_t level = 0; level < levels; ++level)
            book.submit_order({id++, lob::Side::Sell, 100 + static_cast<lob::Price>(level), 1});
        const auto trades = book.submit_order({id++, lob::Side::Buy, 131, levels});
        if (trades.size() != levels) return false;
    }
    const auto stop = Clock::now();
    report("32-level submissions", batches * (levels + 1), start, stop);
    return book.order_count() == 0;
}

bool partial_multi_order(std::uint64_t batches)
{
    lob::OrderBook book;
    lob::OrderId id = 1;
    const auto start = Clock::now();
    for (std::uint64_t batch = 0; batch < batches; ++batch)
    {
        const lob::OrderId first_id = id;
        for (int order = 0; order < 8; ++order)
            book.add_order({id++, lob::Side::Sell, 100, 2});
        const auto trades = book.submit_order({id++, lob::Side::Buy, 100, 9});
        if (trades.size() != 5 || trades.back().quantity != 1) return false;
        for (lob::OrderId resting_id = first_id + 4; resting_id < first_id + 8; ++resting_id)
            if (!book.cancel_order(resting_id)) return false;
    }
    const auto stop = Clock::now();
    report("partial/multi-order", batches * 13, start, stop);
    return book.order_count() == 0;
}

bool cancellation_heavy(std::uint64_t count)
{
    lob::OrderBook book;
    std::vector<lob::OrderId> ids;
    ids.reserve(count);
    for (std::uint64_t id = 1; id <= count; ++id)
    {
        if (!book.add_order({id, lob::Side::Buy, 100, 1})) return false;
        ids.push_back(id);
    }
    const auto start = Clock::now();
    for (const auto id : ids)
        if (!book.cancel_order(id)) return false;
    const auto stop = Clock::now();
    report("cancellations (setup out)", count, start, stop);
    return book.order_count() == 0 && !book.best_bid();
}

bool accumulating(std::uint64_t count)
{
    lob::OrderBook book;
    const auto start = Clock::now();
    for (std::uint64_t id = 1; id <= count; ++id)
        if (book.submit_order({id, lob::Side::Buy, 100 + static_cast<lob::Price>(id % 100), 1}).size())
            return false;
    const auto stop = Clock::now();
    report("non-crossing submissions", count, start, stop);
    return book.order_count() == count && book.best_bid() == 199;
}

bool queries(std::uint64_t iterations)
{
    lob::OrderBook book;
    for (std::uint64_t level = 0; level < 50; ++level)
    {
        if (!book.add_order({level + 1, lob::Side::Buy, 100 + static_cast<lob::Price>(level), 3}) ||
            !book.add_order({level + 100, lob::Side::Sell, 200 + static_cast<lob::Price>(level), 4}))
            return false;
    }
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i)
    {
        checksum += *book.best_bid() + *book.best_ask();
        checksum += *book.bid_quantity_at(100 + static_cast<lob::Price>(i % 50));
        checksum += *book.ask_quantity_at(200 + static_cast<lob::Price>(i % 50));
    }
    const auto stop = Clock::now();
    report("best/level queries", iterations * 4, start, stop);
    return checksum == iterations * 356;
}

bool mixed(std::uint64_t iterations)
{
    lob::OrderBook book;
    lob::OrderId id = 1;
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i)
    {
        if (!book.add_order({id++, lob::Side::Buy, 99, 2})) return false;
        if (!book.add_order({id++, lob::Side::Sell, 101, 2})) return false;
        const auto trades = book.submit_order({id++, lob::Side::Buy, 101, 1});
        if (trades.size() != 1 || trades.front().quantity != 1) return false;
        if (!book.cancel_order(id - 3)) return false;
        if (!book.cancel_order(id - 2)) return false;
    }
    const auto stop = Clock::now();
    report("mixed submits/cancels", iterations * 5, start, stop);
    return book.order_count() == 0;
}

std::int64_t quantile(const std::vector<std::int64_t> &values, double probability)
{
    const auto index = static_cast<std::size_t>(probability * (values.size() - 1));
    return values[index];
}

void latency(std::size_t samples)
{
    lob::OrderBook book;
    std::vector<std::int64_t> durations;
    durations.reserve(samples);
    for (std::size_t i = 0; i < samples; ++i)
    {
        if (!book.add_order({1, lob::Side::Sell, 100, 1})) std::exit(EXIT_FAILURE);
        const auto start = Clock::now();
        const auto trades = book.submit_order({2, lob::Side::Buy, 100, 1});
        const auto stop = Clock::now();
        if (trades.size() != 1 || book.order_count() != 0) std::exit(EXIT_FAILURE);
        durations.push_back(std::chrono::duration_cast<Nanoseconds>(stop - start).count());
    }
    std::sort(durations.begin(), durations.end());
    std::cout << "single submit latency_ns samples=" << samples
              << " p50=" << quantile(durations, 0.50)
              << " p90=" << quantile(durations, 0.90)
              << " p95=" << quantile(durations, 0.95)
              << " p99=" << quantile(durations, 0.99);
    if (samples >= 1000) std::cout << " p99.9=" << quantile(durations, 0.999);
    std::cout << " sample_max=" << durations.back() << '\n';
}
} // namespace

int main()
{
    constexpr std::uint64_t normal_count = 100'000;
    std::cout << "C++ Limit Order Book benchmark; elapsed time excludes output; setup excluded only where stated.\n";
    if (!single_price(normal_count) ||
        !event_enabled_matching(normal_count) ||
        !multiple_levels(normal_count / 10) ||
        !partial_multi_order(normal_count / 10) ||
        !cancellation_heavy(100'000) ||
        !accumulating(normal_count) ||
        !queries(250'000) ||
        !mixed(normal_count / 2))
    {
        std::cerr << "Workload verification failed\n";
        return EXIT_FAILURE;
    }
    latency(50'000);
}
