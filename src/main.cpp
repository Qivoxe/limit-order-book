#include "lob/exchange_simulator.hpp"

#include <iostream>
#include <string>

namespace
{
void print_quote(const lob::ExchangeSimulator &exchange)
{
    const auto bid = exchange.best_bid();
    const auto ask = exchange.best_ask();
    std::cout << "Best bid: " << (bid ? std::to_string(*bid) : "none")
              << " | Best ask: " << (ask ? std::to_string(*ask) : "none") << '\n';
}

void print_depth(const lob::BookSnapshot &snapshot)
{
    std::cout << "Depth (price quantity orders)\nBids:\n";
    for (const auto &level : snapshot.bids)
        std::cout << "  " << level.price << ' ' << level.total_quantity << ' '
                  << level.order_count << '\n';
    std::cout << "Asks:\n";
    for (const auto &level : snapshot.asks)
        std::cout << "  " << level.price << ' ' << level.total_quantity << ' '
                  << level.order_count << '\n';
}

void print_trades(const lob::SubmissionResult &result)
{
    for (const auto &trade : result.trades)
        std::cout << "Trade: buy=" << trade.buy_order_id
                  << " sell=" << trade.sell_order_id
                  << " price=" << trade.price
                  << " quantity=" << trade.quantity << '\n';
}
} // namespace

int main()
{
    lob::ExchangeSimulator exchange;
    std::cout << "Exchange simulator demonstration\n";
    exchange.submit_limit_order({1, lob::Side::Buy, 99, 5});
    exchange.submit_limit_order({2, lob::Side::Sell, 101, 3});
    print_trades(exchange.submit_limit_order({3, lob::Side::Sell, 99, 2}));
    print_quote(exchange);
    print_depth(exchange.market_depth(5));

    const auto cancellation = exchange.cancel_order(2);
    std::cout << "Cancel order 2: " << (cancellation.cancelled ? "success" : "failed") << '\n';
    print_quote(exchange);
    return 0;
}
