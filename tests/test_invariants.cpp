
#include <gtest/gtest.h>

#include "lob/order_book.hpp"

TEST(InvariantTest, PartialFillUpdatesRemainingQuantity)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Sell, 100, 10}));

    const auto trades =
        book.submit_order({2, lob::Side::Buy, 100, 4});

    ASSERT_EQ(trades.size(), 1);
    ASSERT_TRUE(book.ask_quantity_at(100).has_value());
    EXPECT_EQ(*book.ask_quantity_at(100), 6);
}

TEST(InvariantTest, CancellationUpdatesAggregateQuantity)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Buy, 100, 10}));
    ASSERT_TRUE(book.add_order({2, lob::Side::Buy, 100, 7}));

    ASSERT_TRUE(book.cancel_order(1));

    ASSERT_TRUE(book.bid_quantity_at(100).has_value());
    EXPECT_EQ(*book.bid_quantity_at(100), 7);
}

TEST(InvariantTest, EmptyPriceLevelIsRemoved)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Sell, 100, 10}));
    ASSERT_TRUE(book.cancel_order(1));

    EXPECT_FALSE(book.ask_quantity_at(100).has_value());
}

TEST(InvariantTest, FullyFilledOrderIdCanBeReused)
{
    lob::OrderBook book;

    ASSERT_TRUE(book.add_order({1, lob::Side::Sell, 100, 5}));

    const auto trades =
        book.submit_order({2, lob::Side::Buy, 100, 5});

    ASSERT_EQ(trades.size(), 1);

    // Current implementation removes fully filled IDs from its index.
    EXPECT_TRUE(book.add_order({1, lob::Side::Buy, 99, 3}));
    EXPECT_EQ(book.order_count(), 1);
}
