
#include <gtest/gtest.h>

#include "lob/order_book.hpp"

TEST(CancellationTest, CancelsBuyOrder)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Buy, 100, 10}));
    ASSERT_TRUE(book.cancel_order(1));

    EXPECT_EQ(book.order_count(), 0);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTest, CancelsBestAskAndRevealsNext)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Sell, 100, 5}));
    ASSERT_TRUE(book.add_order({2, lob::Side::Sell, 105, 5}));

    ASSERT_TRUE(book.cancel_order(1));

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 105);
    EXPECT_EQ(book.order_count(), 1);
}

TEST(CancellationTest, RejectsUnknownOrderId)
{
    lob::OrderBook book;

    EXPECT_FALSE(book.cancel_order(999));
}

TEST(CancellationTest, CancelsOneOfTwoOrdersAtSamePrice)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Buy, 100, 5}));
    ASSERT_TRUE(book.add_order({2, lob::Side::Buy, 100, 8}));

    ASSERT_TRUE(book.cancel_order(1));

    EXPECT_EQ(book.order_count(), 1);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 100);
}
