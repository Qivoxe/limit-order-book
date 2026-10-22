
#include "lob/order_book.hpp"

#include <algorithm>
#include <limits>
#include <iterator>
#include <map>

namespace lob
{

    bool OrderBook::add_order(const Order &order)
    {
        if (order.id == 0 || order.price <= 0 || order.quantity == 0)
        {
            return false;
        }

        if (order.side != Side::Buy && order.side != Side::Sell)
        {
            return false;
        }

        if (order_locations_.contains(order.id))
        {
            return false;
        }

        const auto current_quantity = order.side == Side::Buy
            ? bid_quantity_at(order.price)
            : ask_quantity_at(order.price);
        if (current_quantity && order.quantity >
                std::numeric_limits<Quantity>::max() - *current_quantity)
        {
            return false;
        }

        if (order.side == Side::Buy)
        {
            auto &level = bids_[order.price];
            try
            {
                level.orders.push_back(order);
                const auto order_it = std::prev(level.orders.end());
                order_locations_.emplace(
                    order.id,
                    OrderLocation{order.side, order.price, order_it});
                level.total_quantity += order.quantity;
            }
            catch (...)
            {
                if (!level.orders.empty() && level.orders.back().id == order.id)
                    level.orders.pop_back();
                if (level.orders.empty()) bids_.erase(order.price);
                throw;
            }
        }
        else
        {
            auto &level = asks_[order.price];
            try
            {
                level.orders.push_back(order);
                const auto order_it = std::prev(level.orders.end());
                order_locations_.emplace(
                    order.id,
                    OrderLocation{order.side, order.price, order_it});
                level.total_quantity += order.quantity;
            }
            catch (...)
            {
                if (!level.orders.empty() && level.orders.back().id == order.id)
                    level.orders.pop_back();
                if (level.orders.empty()) asks_.erase(order.price);
                throw;
            }
        }

        return true;
    }

    std::vector<Trade> OrderBook::submit_order(Order order)
    {
        std::vector<Trade> trades;

        if (order.id == 0 ||
            order.price <= 0 ||
            order.quantity == 0 ||
            (order.side != Side::Buy && order.side != Side::Sell) ||
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

    SubmissionResult OrderBook::submit_order_with_events(Order order)
    {
        SubmissionResult result{false, {}, {}};
        if (order.id == 0 || order.price <= 0 || order.quantity == 0 ||
            (order.side != Side::Buy && order.side != Side::Sell) ||
            order_locations_.contains(order.id))
        {
            return result;
        }

        const auto old_bid = best_bid();
        const auto old_ask = best_ask();
        Quantity remaining_quantity = order.quantity;
        using Key = std::pair<Side, Price>;
        std::map<Key, std::optional<DepthLevel>> before;
        const auto remember = [&before](Side side, Price price, const auto &levels)
        {
            const auto found = levels.find(price);
            if (found == levels.end())
                before.emplace(Key{side, price}, std::nullopt);
            else
                before.emplace(Key{side, price}, DepthLevel{price,
                    found->second.total_quantity, found->second.orders.size()});
        };

        if (order.side == Side::Buy)
        {
            for (const auto &[price, level] : asks_)
            {
                if (price > order.price) break;
                before.emplace(Key{Side::Sell, price}, DepthLevel{
                    price, level.total_quantity, level.orders.size()});
                remaining_quantity -= std::min(remaining_quantity, level.total_quantity);
            }
            remember(Side::Buy, order.price, bids_);
            const auto resting_quantity = bid_quantity_at(order.price);
            if (remaining_quantity > 0 && resting_quantity &&
                remaining_quantity > std::numeric_limits<Quantity>::max() - *resting_quantity)
                return result;
        }
        else
        {
            for (const auto &[price, level] : bids_)
            {
                if (price < order.price) break;
                before.emplace(Key{Side::Buy, price}, DepthLevel{
                    price, level.total_quantity, level.orders.size()});
                remaining_quantity -= std::min(remaining_quantity, level.total_quantity);
            }
            remember(Side::Sell, order.price, asks_);
            const auto resting_quantity = ask_quantity_at(order.price);
            if (remaining_quantity > 0 && resting_quantity &&
                remaining_quantity > std::numeric_limits<Quantity>::max() - *resting_quantity)
                return result;
        }

        result.trades = submit_order(order);
        result.accepted = true;
        for (const auto &trade : result.trades)
            result.events.emplace_back(TradeEvent{trade});

        for (const auto &[key, old_level] : before)
        {
            const auto [side, price] = key;
            std::optional<DepthLevel> new_level;
            if (side == Side::Buy)
            {
                const auto found = bids_.find(price);
                if (found != bids_.end()) new_level = DepthLevel{
                    price, found->second.total_quantity, found->second.orders.size()};
            }
            else
            {
                const auto found = asks_.find(price);
                if (found != asks_.end()) new_level = DepthLevel{
                    price, found->second.total_quantity, found->second.orders.size()};
            }
            if (old_level == new_level) continue;
            const auto change = !old_level ? LevelChange::Added
                : !new_level ? LevelChange::Removed : LevelChange::Updated;
            result.events.emplace_back(PriceLevelEvent{side, price, change,
                new_level ? new_level->total_quantity : 0,
                new_level ? new_level->order_count : 0});
        }
        if (old_bid != best_bid() || old_ask != best_ask())
            result.events.emplace_back(BestQuoteEvent{best_bid(), best_ask()});
        return result;
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

    CancellationResult OrderBook::cancel_order_with_events(OrderId order_id)
    {
        const auto location_it = order_locations_.find(order_id);
        if (location_it == order_locations_.end()) return {false, {}};

        const auto location = location_it->second;
        const auto old_bid = best_bid();
        const auto old_ask = best_ask();
        if (!cancel_order(order_id)) return {false, {}};

        CancellationResult result{true, {}};
        const auto new_quantity = location.side == Side::Buy
            ? bid_quantity_at(location.price) : ask_quantity_at(location.price);
        const std::size_t new_count = location.side == Side::Buy
            ? (bids_.contains(location.price) ? bids_.find(location.price)->second.orders.size() : 0)
            : (asks_.contains(location.price) ? asks_.find(location.price)->second.orders.size() : 0);
        const auto change = new_count == 0 ? LevelChange::Removed : LevelChange::Updated;
        result.events.emplace_back(PriceLevelEvent{location.side, location.price, change,
            new_quantity.value_or(0), new_count});
        if (old_bid != best_bid() || old_ask != best_ask())
            result.events.emplace_back(BestQuoteEvent{best_bid(), best_ask()});
        return result;
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

    BookSnapshot OrderBook::depth_snapshot(std::size_t depth) const
    {
        BookSnapshot snapshot;
        snapshot.bids.reserve(std::min(depth, bids_.size()));
        snapshot.asks.reserve(std::min(depth, asks_.size()));
        for (auto it = bids_.begin(); it != bids_.end() && snapshot.bids.size() < depth; ++it)
            snapshot.bids.push_back({it->first, it->second.total_quantity, it->second.orders.size()});
        for (auto it = asks_.begin(); it != asks_.end() && snapshot.asks.size() < depth; ++it)
            snapshot.asks.push_back({it->first, it->second.total_quantity, it->second.orders.size()});
        return snapshot;
    }

} // namespace lob
