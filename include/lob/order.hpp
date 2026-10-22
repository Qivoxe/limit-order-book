
#pragma once

#include <cstdint>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;       // Integer price ticks
using Quantity = std::uint64_t;

enum class Side {
    Buy,
    Sell
};

struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};

}  // namespace lob
