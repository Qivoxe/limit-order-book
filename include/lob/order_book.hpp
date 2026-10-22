
#pragma once

#include "lob/order.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob
{

    struct Trade
    {
        OrderId buy_order_id;
        OrderId sell_order_id;
        Price price;
        Quantity quantity;
    };

    struct PriceLevel
    {
        std::deque<Order> orders;
        Quantity total_quantity{0};
    };

    struct OrderLocation
    {
        Side side;
        Price price;
    };

    class OrderBook
    {
    public:
        using BidLevels =
            std::map<Price, PriceLevel, std::greater<Price>>;

        using AskLevels =
            std::map<Price, PriceLevel, std::less<Price>>;

        bool add_order(const Order &order);

        std::vector<Trade> submit_order(Order order);

        bool cancel_order(OrderId order_id);

        std::optional<Price> best_bid() const;
        std::optional<Price> best_ask() const;
        std::optional<Quantity> bid_quantity_at(Price price) const;
        std::optional<Quantity> ask_quantity_at(Price price) const;

        std::size_t order_count() const noexcept;

    private:
        BidLevels bids_;
        AskLevels asks_;

        std::unordered_map<OrderId, OrderLocation> order_locations_;

        void match_buy(Order &incoming, std::vector<Trade> &trades);
        void match_sell(Order &incoming, std::vector<Trade> &trades);
    };

} // namespace lob
