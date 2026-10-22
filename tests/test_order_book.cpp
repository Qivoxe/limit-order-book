
#include <gtest/gtest.h>

#include "lob/order_book.hpp"

TEST(OrderBookTest, StartsEmpty)
{
    lob::OrderBook book;

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.order_count(), 0);
}

TEST(OrderBookTest, FindsBestBidAndAsk)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Buy, 100, 10}));
    EXPECT_TRUE(book.add_order({2, lob::Side::Buy, 105, 5}));
    EXPECT_TRUE(book.add_order({3, lob::Side::Sell, 110, 7}));
    EXPECT_TRUE(book.add_order({4, lob::Side::Sell, 108, 3}));

    ASSERT_TRUE(book.best_bid().has_value());
    ASSERT_TRUE(book.best_ask().has_value());

    EXPECT_EQ(*book.best_bid(), 105);
    EXPECT_EQ(*book.best_ask(), 108);
    EXPECT_EQ(book.order_count(), 4);
}

TEST(OrderBookTest, RejectsDuplicateOrderIds)
{
    lob::OrderBook book;

    EXPECT_TRUE(book.add_order({1, lob::Side::Buy, 100, 10}));
    EXPECT_FALSE(book.add_order({1, lob::Side::Sell, 110, 5}));

    EXPECT_EQ(book.order_count(), 1);
}

TEST(OrderBookTest, RejectsInvalidOrders)
{
    lob::OrderBook book;

    EXPECT_FALSE(book.add_order({0, lob::Side::Buy, 100, 10}));
    EXPECT_FALSE(book.add_order({1, lob::Side::Buy, 0, 10}));
    EXPECT_FALSE(book.add_order({2, lob::Side::Sell, 100, 0}));

    EXPECT_EQ(book.order_count(), 0);
}
