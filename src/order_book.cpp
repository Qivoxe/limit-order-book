
#include "lob/order_book.hpp"

#include <algorithm>
#include <iterator>

namespace lob
{

    bool OrderBook::add_order(const Order &order)
    {
        if (order.id == 0 || order.price <= 0 || order.quantity == 0)
        {
            return false;
        }

        if (order_locations_.contains(order.id))
        {
            return false;
        }

        if (order.side == Side::Buy)
        {
            auto &level = bids_[order.price];
            level.orders.push_back(order);
            level.total_quantity += order.quantity;

            const auto order_it = std::prev(level.orders.end());

            order_locations_.emplace(
                order.id,
                OrderLocation{order.side, order.price, order_it});
        }
        else
        {
            auto &level = asks_[order.price];
            level.orders.push_back(order);
            level.total_quantity += order.quantity;

            const auto order_it = std::prev(level.orders.end());

            order_locations_.emplace(
                order.id,
                OrderLocation{order.side, order.price, order_it});
        }

        return true;
    }

    std::vector<Trade> OrderBook::submit_order(Order order)
    {
        std::vector<Trade> trades;

        if (order.id == 0 ||
            order.price <= 0 ||
            order.quantity == 0 ||
            order_locations_.contains(order.id))
        {
            return trades;
        }

        if (order.side == Side::Buy)
        {
            match_buy(order, trades);
        }
        else
        {
            match_sell(order, trades);
        }

        // Rest any unfilled quantity on the book.
        if (order.quantity > 0)
        {
            add_order(order);
        }

        return trades;
    }

    void OrderBook::match_buy(
        Order &incoming,
        std::vector<Trade> &trades)
    {
        while (incoming.quantity > 0 && !asks_.empty())
        {
            auto level_it = asks_.begin();

            // Buy limit price cannot be below the best ask.
            if (level_it->first > incoming.price)
            {
                break;
            }

            auto &level = level_it->second;
            auto &resting = level.orders.front();

            const Quantity executed =
                std::min(incoming.quantity, resting.quantity);

            // The resting order determines the execution price.
            trades.push_back({incoming.id,
                              resting.id,
                              resting.price,
                              executed});

            incoming.quantity -= executed;
            resting.quantity -= executed;
            level.total_quantity -= executed;

            if (resting.quantity == 0)
            {
                // Remove the index before invalidating its list iterator.
                order_locations_.erase(resting.id);
                level.orders.pop_front();
            }

            if (level.orders.empty())
            {
                asks_.erase(level_it);
            }
        }
    }

    void OrderBook::match_sell(
        Order &incoming,
        std::vector<Trade> &trades)
    {
        while (incoming.quantity > 0 && !bids_.empty())
        {
            auto level_it = bids_.begin();

            // Sell limit price cannot exceed the best bid.
            if (level_it->first < incoming.price)
            {
                break;
            }

            auto &level = level_it->second;
            auto &resting = level.orders.front();

            const Quantity executed =
                std::min(incoming.quantity, resting.quantity);

            trades.push_back({resting.id,
                              incoming.id,
                              resting.price,
                              executed});

            incoming.quantity -= executed;
            resting.quantity -= executed;
            level.total_quantity -= executed;

            if (resting.quantity == 0)
            {
                order_locations_.erase(resting.id);
                level.orders.pop_front();
            }

            if (level.orders.empty())
            {
                bids_.erase(level_it);
            }
        }
    }

    bool OrderBook::cancel_order(OrderId order_id)
    {
        const auto location_it = order_locations_.find(order_id);

        if (location_it == order_locations_.end())
        {
            return false;
        }

        const OrderLocation location = location_it->second;

        if (location.side == Side::Buy)
        {
            auto level_it = bids_.find(location.price);

            if (level_it == bids_.end())
            {
                return false;
            }

            auto &level = level_it->second;

            level.total_quantity -= location.order_it->quantity;
            level.orders.erase(location.order_it);

            if (level.orders.empty())
            {
                bids_.erase(level_it);
            }
        }
        else
        {
            auto level_it = asks_.find(location.price);

            if (level_it == asks_.end())
            {
                return false;
            }

            auto &level = level_it->second;

            level.total_quantity -= location.order_it->quantity;
            level.orders.erase(location.order_it);

            if (level.orders.empty())
            {
                asks_.erase(level_it);
            }
        }

        order_locations_.erase(location_it);
        return true;
    }

    std::optional<Price> OrderBook::best_bid() const
    {
        if (bids_.empty())
        {
            return std::nullopt;
        }

        return bids_.begin()->first;
    }

    std::optional<Price> OrderBook::best_ask() const
    {
        if (asks_.empty())
        {
            return std::nullopt;
        }

        return asks_.begin()->first;
    }

    std::size_t OrderBook::order_count() const noexcept
    {
        return order_locations_.size();
    }

    std::optional<Quantity> OrderBook::bid_quantity_at(Price price) const
    {
        const auto it = bids_.find(price);

        if (it == bids_.end())
        {
            return std::nullopt;
        }

        return it->second.total_quantity;
    }

    std::optional<Quantity> OrderBook::ask_quantity_at(Price price) const
    {
        const auto it = asks_.find(price);

        if (it == asks_.end())
        {
            return std::nullopt;
        }

        return it->second.total_quantity;
    }

} // namespace lob
