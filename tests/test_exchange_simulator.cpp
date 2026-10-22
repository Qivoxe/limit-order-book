#include <gtest/gtest.h>

#include "lob/exchange_simulator.hpp"

#include <limits>

TEST(DepthSnapshotTest, OrdersLevelsAndAggregatesWithoutMutatingBook)
{
    lob::OrderBook book;
    ASSERT_TRUE(book.add_order({1, lob::Side::Buy, 100, 2}));
    ASSERT_TRUE(book.add_order({2, lob::Side::Buy, 100, 3}));
    ASSERT_TRUE(book.add_order({3, lob::Side::Buy, 105, 4}));
    ASSERT_TRUE(book.add_order({4, lob::Side::Sell, 110, 6}));
    ASSERT_TRUE(book.add_order({5, lob::Side::Sell, 108, 7}));

    const auto snapshot = book.depth_snapshot(1);
    ASSERT_EQ(snapshot.bids.size(), 1);
    ASSERT_EQ(snapshot.asks.size(), 1);
    EXPECT_EQ(snapshot.bids[0], (lob::DepthLevel{105, 4, 1}));
    EXPECT_EQ(snapshot.asks[0], (lob::DepthLevel{108, 7, 1}));
    EXPECT_EQ(book.order_count(), 5);

    const auto full_depth = book.depth_snapshot(10);
    ASSERT_EQ(full_depth.bids.size(), 2);
    ASSERT_EQ(full_depth.asks.size(), 2);
    EXPECT_EQ(full_depth.bids[1], (lob::DepthLevel{100, 5, 2}));
    EXPECT_EQ(full_depth.asks[1], (lob::DepthLevel{110, 6, 1}));
}

TEST(DepthSnapshotTest, EmptyBookAndZeroDepthReturnEmptySnapshots)
{
    lob::OrderBook book;
    EXPECT_TRUE(book.depth_snapshot(5).bids.empty());
    EXPECT_TRUE(book.depth_snapshot(5).asks.empty());
    ASSERT_TRUE(book.add_order({1, lob::Side::Buy, 100, 1}));
    EXPECT_TRUE(book.depth_snapshot(0).bids.empty());
    EXPECT_TRUE(book.depth_snapshot(0).asks.empty());
}

TEST(ExchangeSimulatorTest, EmitsTradeThenLevelEventsAndOnlyChangedQuote)
{
    lob::ExchangeSimulator exchange;
    const auto ask = exchange.submit_limit_order({1, lob::Side::Sell, 101, 3});
    ASSERT_TRUE(ask.accepted);
    ASSERT_EQ(ask.events.size(), 2);
    const auto *added = std::get_if<lob::PriceLevelEvent>(&ask.events[0]);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->side, lob::Side::Sell);
    EXPECT_EQ(added->price, 101);
    EXPECT_EQ(added->change, lob::LevelChange::Added);
    EXPECT_EQ(added->total_quantity, 3);
    EXPECT_EQ(added->order_count, 1);
    ASSERT_NE(std::get_if<lob::BestQuoteEvent>(&ask.events[1]), nullptr);

    const auto bid = exchange.submit_limit_order({2, lob::Side::Buy, 101, 1});
    ASSERT_TRUE(bid.accepted);
    ASSERT_EQ(bid.trades.size(), 1);
    ASSERT_EQ(bid.events.size(), 2);
    const auto *trade = std::get_if<lob::TradeEvent>(&bid.events[0]);
    ASSERT_NE(trade, nullptr);
    EXPECT_EQ(trade->trade.buy_order_id, 2);
    EXPECT_EQ(trade->trade.sell_order_id, 1);
    EXPECT_EQ(trade->trade.price, 101);
    EXPECT_EQ(trade->trade.quantity, 1);
    const auto *updated = std::get_if<lob::PriceLevelEvent>(&bid.events[1]);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->change, lob::LevelChange::Updated);
    EXPECT_EQ(updated->total_quantity, 2);
    EXPECT_EQ(updated->order_count, 1);

    const auto fill = exchange.submit_limit_order({3, lob::Side::Buy, 101, 2});
    ASSERT_EQ(fill.events.size(), 3);
    EXPECT_NE(std::get_if<lob::TradeEvent>(&fill.events[0]), nullptr);
    const auto *removed = std::get_if<lob::PriceLevelEvent>(&fill.events[1]);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->change, lob::LevelChange::Removed);
    EXPECT_NE(std::get_if<lob::BestQuoteEvent>(&fill.events[2]), nullptr);
    EXPECT_TRUE(exchange.submit_limit_order({1, lob::Side::Buy, 99, 1}).accepted);
}

TEST(ExchangeSimulatorTest, ReportsCancellationAndRejectsNoOpsWithoutEvents)
{
    lob::ExchangeSimulator exchange;
    const auto added = exchange.submit_limit_order({7, lob::Side::Buy, 99, 4});
    ASSERT_TRUE(added.accepted);
    const auto duplicate = exchange.submit_limit_order({7, lob::Side::Sell, 110, 2});
    EXPECT_FALSE(duplicate.accepted);
    EXPECT_TRUE(duplicate.trades.empty());
    EXPECT_TRUE(duplicate.events.empty());

    const auto cancelled = exchange.cancel_order(7);
    ASSERT_TRUE(cancelled.cancelled);
    ASSERT_EQ(cancelled.events.size(), 2);
    const auto *removed = std::get_if<lob::PriceLevelEvent>(&cancelled.events[0]);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->change, lob::LevelChange::Removed);
    EXPECT_EQ(removed->total_quantity, 0);
    ASSERT_NE(std::get_if<lob::BestQuoteEvent>(&cancelled.events[1]), nullptr);

    const auto missing = exchange.cancel_order(7);
    EXPECT_FALSE(missing.cancelled);
    EXPECT_TRUE(missing.events.empty());
    EXPECT_TRUE(exchange.submit_limit_order({7, lob::Side::Sell, 110, 2}).accepted);
}

TEST(ExchangeSimulatorTest, RejectsAggregateOverflowWithoutChangingBook)
{
    lob::ExchangeSimulator exchange;
    ASSERT_TRUE(exchange.submit_limit_order({1, lob::Side::Buy, 100,
        std::numeric_limits<lob::Quantity>::max()}).accepted);

    const auto rejected = exchange.submit_limit_order({2, lob::Side::Buy, 100, 1});
    EXPECT_FALSE(rejected.accepted);
    EXPECT_TRUE(rejected.trades.empty());
    EXPECT_TRUE(rejected.events.empty());
    ASSERT_EQ(exchange.market_depth(1).bids.size(), 1);
    EXPECT_EQ(exchange.market_depth(1).bids.front().total_quantity,
              std::numeric_limits<lob::Quantity>::max());
    EXPECT_EQ(exchange.order_count(), 1);
}
