
#include <gtest/gtest.h>

#include "lob/order_book.hpp"

TEST(MatchingTest, ExecutesCrossingOrders)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Sell, 100, 10}));

    const auto trades =
        book.submit_order({2, lob::Side::Buy, 100, 10});

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_EQ(book.order_count(), 0);
}

TEST(MatchingTest, SupportsPartialFill)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Sell, 100, 10}));

    const auto trades =
        book.submit_order({2, lob::Side::Buy, 100, 4});

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 4);
    EXPECT_EQ(book.order_count(), 1);
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 100);
}

TEST(MatchingTest, PreservesPricePriority)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Sell, 102, 5}));
    EXPECT_TRUE(book.add_order({2, lob::Side::Sell, 100, 5}));

    const auto trades =
        book.submit_order({3, lob::Side::Buy, 105, 5});

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, 2);
    EXPECT_EQ(trades[0].price, 100);
}

TEST(MatchingTest, DoesNotMatchNonCrossingOrders)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Sell, 110, 5}));

    const auto trades =
        book.submit_order({2, lob::Side::Buy, 100, 5});

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 2);
}

TEST(MatchingTest, PreservesFifoAtSamePrice)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Sell, 100, 5}));
    EXPECT_TRUE(book.add_order({2, lob::Side::Sell, 100, 5}));

    const auto trades =
        book.submit_order({3, lob::Side::Buy, 100, 5});

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, 1);
}

TEST(MatchingTest, MatchesMultipleRestingOrders)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Sell, 100, 3}));
    EXPECT_TRUE(book.add_order({2, lob::Side::Sell, 101, 4}));

    const auto trades =
        book.submit_order({3, lob::Side::Buy, 101, 6});

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 3);
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(trades[1].quantity, 3);
}

TEST(MatchingTest, ReportsExecutionOrderAcrossPriceLevelsAndFifo)
{
    lob::OrderBook book;
    ASSERT_TRUE(book.add_order({1, lob::Side::Sell, 102, 2}));
    ASSERT_TRUE(book.add_order({2, lob::Side::Sell, 100, 3}));
    ASSERT_TRUE(book.add_order({3, lob::Side::Sell, 100, 4}));

    const auto trades = book.submit_order({4, lob::Side::Buy, 102, 8});
    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].sell_order_id, 2);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 3);
    EXPECT_EQ(trades[1].sell_order_id, 3);
    EXPECT_EQ(trades[1].price, 100);
    EXPECT_EQ(trades[1].quantity, 4);
    EXPECT_EQ(trades[2].sell_order_id, 1);
    EXPECT_EQ(trades[2].price, 102);
    EXPECT_EQ(trades[2].quantity, 1);
    for (const auto &trade : trades)
    {
        EXPECT_EQ(trade.buy_order_id, 4);
        EXPECT_GT(trade.quantity, 0);
    }
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.ask_quantity_at(102), 1);
}
