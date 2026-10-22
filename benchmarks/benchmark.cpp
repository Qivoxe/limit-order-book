
#include "lob/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

static void run_throughput_benchmark(std::uint64_t pairs)
{
    lob::OrderBook book;
    lob::OrderId next_id = 1;

    // Warm up the engine before measuring.
    for (std::uint64_t i = 0; i < 100'000; ++i)
    {
        book.submit_order({next_id++, lob::Side::Sell, 100, 10});
        book.submit_order({next_id++, lob::Side::Buy, 100, 10});
    }

    const auto start = Clock::now();

    for (std::uint64_t i = 0; i < pairs; ++i)
    {
        book.submit_order({next_id++, lob::Side::Sell, 100, 10});
        book.submit_order({next_id++, lob::Side::Buy, 100, 10});
    }

    const auto end = Clock::now();

    const auto elapsed_ns =
        std::chrono::duration_cast<Nanoseconds>(end - start).count();

    const double seconds = elapsed_ns / 1'000'000'000.0;
    const double submissions = static_cast<double>(pairs) * 2.0;

    std::cout << "\nThroughput benchmark\n";
    std::cout << "--------------------\n";
    std::cout << "Order submissions: " << submissions << '\n';
    std::cout << "Elapsed time:      " << seconds << " seconds\n";
    std::cout << "Throughput:        "
              << std::fixed << std::setprecision(0)
              << submissions / seconds << " submissions/second\n";

    std::cout << "Remaining orders:  " << book.order_count() << '\n';
}

static void run_latency_benchmark(std::size_t samples)
{
    lob::OrderBook book;
    lob::OrderId next_id = 1;

    std::vector<std::int64_t> latencies;
    latencies.reserve(samples);

    // Each sample is a complete two-order execution pair.
    for (std::size_t i = 0; i < samples; ++i)
    {
        const auto start = Clock::now();

        book.submit_order({next_id++, lob::Side::Sell, 100, 10});
        book.submit_order({next_id++, lob::Side::Buy, 100, 10});

        const auto end = Clock::now();

        latencies.push_back(
            std::chrono::duration_cast<Nanoseconds>(
                end - start)
                .count());
    }

    std::sort(latencies.begin(), latencies.end());

    const auto percentile = [&latencies](double p)
    {
        const auto index = static_cast<std::size_t>(
            p * static_cast<double>(latencies.size() - 1));
        return latencies[index];
    };

    std::cout << "\nLatency benchmark\n";
    std::cout << "-----------------\n";
    std::cout << "Samples (order pairs): " << samples << '\n';
    std::cout << "p50: " << percentile(0.50) << " ns\n";
    std::cout << "p95: " << percentile(0.95) << " ns\n";
    std::cout << "p99: " << percentile(0.99) << " ns\n";
    std::cout << "Note: latency is for two submissions, not one.\n";
}

static void run_multi_price_benchmark(std::uint64_t batches)
{
    constexpr std::uint64_t levels = 100;

    lob::OrderBook book;
    lob::OrderId next_id = 1;

    const auto start = Clock::now();

    for (std::uint64_t batch = 0; batch < batches; ++batch)
    {
        // Populate 100 distinct ask price levels.
        for (std::uint64_t i = 0; i < levels; ++i)
        {
            const lob::Price price =
                100 + static_cast<lob::Price>(i);

            book.submit_order({next_id++,
                               lob::Side::Sell,
                               price,
                               1});
        }

        // Consume all 100 levels with one incoming buy.
        const auto trades = book.submit_order({next_id++,
                                               lob::Side::Buy,
                                               200,
                                               levels});

        if (trades.size() != levels)
        {
            std::cerr << "Unexpected trade count in multi-price benchmark\n";
            std::exit(EXIT_FAILURE);
        }
    }

    const auto end = Clock::now();

    const double seconds =
        std::chrono::duration<double>(end - start).count();

    const double submissions =
        static_cast<double>(batches) * (levels + 1);

    std::cout << "\nMulti-price benchmark\n";
    std::cout << "---------------------\n";
    std::cout << "Price levels per batch: " << levels << '\n';
    std::cout << "Order submissions: " << submissions << '\n';

    std::cout << "Elapsed time: "
              << std::fixed << std::setprecision(6)
              << seconds << " seconds\n";

    std::cout << "Throughput: "
              << std::setprecision(0)
              << submissions / seconds
              << " submissions/second\n";

    std::cout << "Remaining orders: "
              << book.order_count() << '\n';
}

int main()
{
    constexpr std::uint64_t throughput_pairs = 1'000'000;
    constexpr std::size_t latency_samples = 100'000;
    constexpr std::uint64_t multi_price_batches = 10'000;

    std::cout << "C++ Limit Order Book Benchmark\n";

    run_throughput_benchmark(throughput_pairs);
    run_latency_benchmark(latency_samples);
    run_multi_price_benchmark(multi_price_batches);

    return 0;
}
