
#include <gtest/gtest.h>

#include "lob/order.hpp"

TEST(OrderTest, StoresBuyOrderFields) {
    const lob::Order order{
        1,
        lob::Side::Buy,
        10050,
        25
    };

    EXPECT_EQ(order.id, 1);
    EXPECT_EQ(order.side, lob::Side::Buy);
    EXPECT_EQ(order.price, 10050);
    EXPECT_EQ(order.quantity, 25);
}

TEST(OrderTest, StoresSellOrderFields) {
    const lob::Order order{
        2,
        lob::Side::Sell,
        10100,
        10
    };

    EXPECT_EQ(order.id, 2);
    EXPECT_EQ(order.side, lob::Side::Sell);
    EXPECT_EQ(order.price, 10100);
    EXPECT_EQ(order.quantity, 10);
}
