#pragma once

#include "lob/order.hpp"

#include <cstddef>
#include <optional>
#include <variant>
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

struct DepthLevel
{
    Price price;
    Quantity total_quantity;
    std::size_t order_count;
    bool operator==(const DepthLevel &) const = default;
};

struct BookSnapshot
{
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
};

struct TradeEvent
{
    Trade trade;
};

enum class LevelChange
{
    Added,
    Updated,
    Removed
};

struct PriceLevelEvent
{
    Side side;
    Price price;
    LevelChange change;
    // Removed levels report zero quantity and zero orders.
    Quantity total_quantity;
    std::size_t order_count;
};

struct BestQuoteEvent
{
    std::optional<Price> bid;
    std::optional<Price> ask;
};

using MarketDataEvent = std::variant<TradeEvent, PriceLevelEvent, BestQuoteEvent>;

struct SubmissionResult
{
    bool accepted;
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;
};

struct CancellationResult
{
    bool cancelled;
    std::vector<MarketDataEvent> events;
};

} // namespace lob
