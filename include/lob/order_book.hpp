
#pragma once

#include "lob/order.hpp"
#include "lob/market_data.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>
#include <list>

namespace lob
{

    struct PriceLevel
    {
        std::list<Order> orders;
        Quantity total_quantity{0};
    };

    struct OrderLocation
    {
        Side side;
        Price price;
        std::list<Order>::iterator order_it;
    };

    class OrderBook
    {
    public:
        using BidLevels =
            std::map<Price, PriceLevel, std::greater<Price>>;

        using AskLevels =
            std::map<Price, PriceLevel, std::less<Price>>;

        // IDs are unique while resting and reusable after full fill or cancel.
        bool add_order(const Order &order);

        std::vector<Trade> submit_order(Order order);

        // Events are returned synchronously: trades in execution order, then
        // changed levels ordered by side and price, then a quote change if any.
        SubmissionResult submit_order_with_events(Order order);

        bool cancel_order(OrderId order_id);

        CancellationResult cancel_order_with_events(OrderId order_id);

        BookSnapshot depth_snapshot(std::size_t depth) const;

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
