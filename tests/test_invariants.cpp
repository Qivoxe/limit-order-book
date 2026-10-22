
#include <gtest/gtest.h>

#include "lob/order_book.hpp"

#include <limits>
#include <random>
#include <unordered_map>

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

TEST(InvariantTest, RejectsQuantityOverflowAndInvalidSide)
{
    lob::OrderBook book;
    ASSERT_TRUE(book.add_order({1, lob::Side::Buy, 100,
                                std::numeric_limits<lob::Quantity>::max()}));
    EXPECT_FALSE(book.add_order({2, lob::Side::Buy, 100, 1}));
    EXPECT_FALSE(book.add_order({3, static_cast<lob::Side>(99), 100, 1}));
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.bid_quantity_at(100), std::numeric_limits<lob::Quantity>::max());
}

TEST(InvariantTest, SeededAddsAndCancellationsPreserveAggregates)
{
    lob::OrderBook book;
    std::unordered_map<lob::OrderId, lob::Order> resting;
    std::mt19937_64 random(0x5eed1234);

    for (lob::OrderId id = 1; id <= 1000; ++id)
    {
        const auto side = (random() & 1) ? lob::Side::Buy : lob::Side::Sell;
        const lob::Price price = 90 + static_cast<lob::Price>(random() % 21);
        const lob::Quantity quantity = 1 + random() % 100;
        ASSERT_TRUE(book.add_order({id, side, price, quantity}));
        resting.emplace(id, lob::Order{id, side, price, quantity});

        if (id % 3 == 0)
        {
            const lob::OrderId cancel_id = 1 + random() % id;
            const auto found = resting.find(cancel_id);
            if (found != resting.end())
            {
                ASSERT_TRUE(book.cancel_order(cancel_id));
                resting.erase(found);
            }
        }

        ASSERT_EQ(book.order_count(), resting.size());
        for (lob::Price checked_price = 90; checked_price <= 110; ++checked_price)
        {
            lob::Quantity expected_bids = 0;
            lob::Quantity expected_asks = 0;
            for (const auto &[resting_id, order] : resting)
            {
                (void)resting_id;
                if (order.price != checked_price) continue;
                (order.side == lob::Side::Buy ? expected_bids : expected_asks) += order.quantity;
            }
            const auto actual_bid = book.bid_quantity_at(checked_price);
            const auto actual_ask = book.ask_quantity_at(checked_price);
            EXPECT_EQ(actual_bid.has_value(), expected_bids != 0);
            EXPECT_EQ(actual_ask.has_value(), expected_asks != 0);
            if (actual_bid) EXPECT_EQ(*actual_bid, expected_bids);
            if (actual_ask) EXPECT_EQ(*actual_ask, expected_asks);
        }

        std::optional<lob::Price> expected_best_bid;
        std::optional<lob::Price> expected_best_ask;
        for (const auto &[resting_id, order] : resting)
        {
            (void)resting_id;
            auto &best = order.side == lob::Side::Buy ? expected_best_bid : expected_best_ask;
            if (!best || (order.side == lob::Side::Buy ? order.price > *best : order.price < *best))
                best = order.price;
        }
        EXPECT_EQ(book.best_bid(), expected_best_bid);
        EXPECT_EQ(book.best_ask(), expected_best_ask);
    }
}
