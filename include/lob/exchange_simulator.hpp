#pragma once

#include "lob/order_book.hpp"

namespace lob
{

class ExchangeSimulator
{
public:
    SubmissionResult submit_limit_order(const Order &order)
    {
        return book_.submit_order_with_events(order);
    }

    CancellationResult cancel_order(OrderId order_id)
    {
        return book_.cancel_order_with_events(order_id);
    }

    std::optional<Price> best_bid() const { return book_.best_bid(); }
    std::optional<Price> best_ask() const { return book_.best_ask(); }
    BookSnapshot market_depth(std::size_t depth) const
    {
        return book_.depth_snapshot(depth);
    }
    std::size_t order_count() const noexcept { return book_.order_count(); }

private:
    OrderBook book_;
};

} // namespace lob
