
#include <iostream>

#include "lob/order.hpp"

int main() {
    const lob::Order order{
        1,
        lob::Side::Buy,
        10050,
        25
    };

    std::cout << "Limit Order Book Engine\n";
    std::cout << "Order ID: " << order.id << '\n';
    std::cout << "Side: "
              << (order.side == lob::Side::Buy ? "BUY" : "SELL")
              << '\n';
    std::cout << "Price (ticks): " << order.price << '\n';
    std::cout << "Quantity: " << order.quantity << '\n';

    return 0;
}
