#include "lob/exchange_simulator.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using Tokens = std::vector<std::string>;

Tokens tokenize(const std::string &line)
{
    std::istringstream input(line);
    Tokens tokens;
    for (std::string token; input >> token;) tokens.push_back(std::move(token));
    return tokens;
}

template <typename Integer>
bool parse_integer(std::string_view text, Integer &value)
{
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

void begin(std::string_view status, std::string_view command, std::string_view reason = {})
{
    std::cout << "BEGIN\t1\t" << status << '\t' << command;
    if (!reason.empty()) std::cout << '\t' << reason;
    std::cout << '\n';
}

void end()
{
    std::cout << "END\n" << std::flush;
}

void print_quote(const lob::ExchangeSimulator &exchange)
{
    const auto bid = exchange.best_bid();
    const auto ask = exchange.best_ask();
    std::cout << "QUOTE\t" << (bid ? std::to_string(*bid) : "-")
              << '\t' << (ask ? std::to_string(*ask) : "-") << '\n';
}

void print_trade(const lob::Trade &trade)
{
    std::cout << "TRADE\t" << trade.buy_order_id << '\t' << trade.sell_order_id
              << '\t' << trade.price << '\t' << trade.quantity << '\n';
}

void print_event(const lob::MarketDataEvent &event)
{
    std::visit([](const auto &value)
    {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, lob::TradeEvent>)
        {
            const auto &trade = value.trade;
            std::cout << "EVENT_TRADE\t" << trade.buy_order_id << '\t'
                      << trade.sell_order_id << '\t' << trade.price << '\t'
                      << trade.quantity << '\n';
        }
        else if constexpr (std::is_same_v<Event, lob::PriceLevelEvent>)
        {
            const char *side = value.side == lob::Side::Buy ? "BID" : "ASK";
            const char *change = value.change == lob::LevelChange::Added ? "ADDED"
                : value.change == lob::LevelChange::Updated ? "UPDATED" : "REMOVED";
            std::cout << "EVENT_LEVEL\t" << side << '\t' << change << '\t'
                      << value.price << '\t' << value.total_quantity << '\t'
                      << value.order_count << '\n';
        }
        else
        {
            const auto bid = value.bid ? std::to_string(*value.bid) : "-";
            const auto ask = value.ask ? std::to_string(*value.ask) : "-";
            std::cout << "EVENT_QUOTE\t" << bid << '\t' << ask << '\n';
        }
    }, event);
}

void print_events(const std::vector<lob::MarketDataEvent> &events)
{
    for (const auto &event : events) print_event(event);
}

void print_snapshot(const lob::ExchangeSimulator &exchange)
{
    const auto snapshot = exchange.market_depth(std::numeric_limits<std::size_t>::max());
    print_quote(exchange);
    std::cout << "COUNT\t" << exchange.order_count() << '\n';
    for (const auto &level : snapshot.bids)
        std::cout << "LEVEL\tBID\t" << level.price << '\t' << level.total_quantity
                  << '\t' << level.order_count << '\n';
    for (const auto &level : snapshot.asks)
        std::cout << "LEVEL\tASK\t" << level.price << '\t' << level.total_quantity
                  << '\t' << level.order_count << '\n';
}

void process(const Tokens &tokens, std::unique_ptr<lob::ExchangeSimulator> &exchange)
{
    if (tokens.empty())
    {
        begin("ERROR", "EMPTY_COMMAND", "BAD_COMMAND");
        end();
        return;
    }

    if (tokens[0] == "SUBMIT")
    {
        if (tokens.size() != 5)
        {
            begin("ERROR", "SUBMIT", "BAD_ARGUMENT_COUNT");
            end();
            return;
        }
        lob::OrderId id{};
        lob::Price price{};
        lob::Quantity quantity{};
        if (!parse_integer(tokens[1], id) || !parse_integer(tokens[3], price) ||
            !parse_integer(tokens[4], quantity))
        {
            begin("ERROR", "SUBMIT", "INVALID_NUMBER");
            end();
            return;
        }
        lob::Side side;
        if (tokens[2] == "BUY") side = lob::Side::Buy;
        else if (tokens[2] == "SELL") side = lob::Side::Sell;
        else
        {
            begin("ERROR", "SUBMIT", "INVALID_SIDE");
            end();
            return;
        }
        const auto result = exchange->submit_limit_order({id, side, price, quantity});
        begin(result.accepted ? "OK" : "REJECTED", "SUBMIT",
              result.accepted ? "" : "INVALID_DUPLICATE_OR_OVERFLOW");
        for (const auto &trade : result.trades) print_trade(trade);
        print_events(result.events);
        end();
        return;
    }

    if (tokens[0] == "CANCEL")
    {
        if (tokens.size() != 2)
        {
            begin("ERROR", "CANCEL", "BAD_ARGUMENT_COUNT");
            end();
            return;
        }
        lob::OrderId id{};
        if (!parse_integer(tokens[1], id))
        {
            begin("ERROR", "CANCEL", "INVALID_NUMBER");
            end();
            return;
        }
        const auto result = exchange->cancel_order(id);
        begin(result.cancelled ? "OK" : "REJECTED", "CANCEL",
              result.cancelled ? "" : "UNKNOWN_ORDER_ID");
        print_events(result.events);
        end();
        return;
    }

    if (tokens[0] == "SNAPSHOT")
    {
        if (tokens.size() != 1)
        {
            begin("ERROR", "SNAPSHOT", "BAD_ARGUMENT_COUNT");
            end();
            return;
        }
        begin("OK", "SNAPSHOT");
        print_snapshot(*exchange);
        end();
        return;
    }

    if (tokens[0] == "RESET")
    {
        if (tokens.size() != 1)
        {
            begin("ERROR", "RESET", "BAD_ARGUMENT_COUNT");
            end();
            return;
        }
        exchange = std::make_unique<lob::ExchangeSimulator>();
        begin("OK", "RESET");
        print_snapshot(*exchange);
        end();
        return;
    }

    if (tokens[0] == "EXIT" && tokens.size() == 1)
    {
        begin("OK", "EXIT");
        end();
        return;
    }

    begin("ERROR", tokens[0], "UNKNOWN_OR_MALFORMED_COMMAND");
    end();
}
} // namespace

int main()
{
    auto exchange = std::make_unique<lob::ExchangeSimulator>();
    std::cout << "READY\t1\n" << std::flush;
    for (std::string line; std::getline(std::cin, line);)
    {
        const auto tokens = tokenize(line);
        try
        {
            process(tokens, exchange);
        }
        catch (const std::exception &error)
        {
            std::cerr << "protocol command failed: " << error.what() << '\n';
            begin("ERROR", tokens.empty() ? "COMMAND" : tokens.front(), "INTERNAL_ERROR");
            end();
        }
        if (tokens.size() == 1 && tokens[0] == "EXIT") break;
    }
}
