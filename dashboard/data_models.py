from __future__ import annotations

from dataclasses import dataclass, field
from typing import Sequence


class ProtocolError(ValueError):
    """The adapter returned a malformed or unsupported protocol frame."""


@dataclass(frozen=True)
class Trade:
    buy_order_id: int
    sell_order_id: int
    price: int
    quantity: int


@dataclass(frozen=True)
class Level:
    side: str
    price: int
    quantity: int
    order_count: int


@dataclass(frozen=True)
class Quote:
    bid: int | None
    ask: int | None


@dataclass(frozen=True)
class Event:
    kind: str
    fields: tuple[str | int, ...]


@dataclass(frozen=True)
class EngineResponse:
    status: str
    operation: str
    reason: str | None = None
    trades: tuple[Trade, ...] = ()
    events: tuple[Event, ...] = ()
    levels: tuple[Level, ...] = ()
    quote: Quote | None = None
    order_count: int | None = None


def _integer(token: str, field_name: str) -> int:
    if not token or not token.isascii() or not token.isdecimal():
        raise ProtocolError(f"Invalid integer for {field_name}")
    return int(token)


def _optional_price(token: str) -> int | None:
    return None if token == "-" else _integer(token, "price")


def parse_frame(lines: Sequence[str]) -> EngineResponse:
    if len(lines) < 2 or lines[-1] != "END":
        raise ProtocolError("Incomplete adapter response frame")
    header = lines[0].split("\t")
    if len(header) not in (4, 5) or header[:2] != ["BEGIN", "1"]:
        raise ProtocolError("Unsupported adapter response header")
    status, operation = header[2:4]
    if status not in {"OK", "REJECTED", "ERROR"}:
        raise ProtocolError("Unsupported adapter response status")

    trades: list[Trade] = []
    events: list[Event] = []
    levels: list[Level] = []
    quote: Quote | None = None
    order_count: int | None = None
    for line in lines[1:-1]:
        parts = line.split("\t")
        kind = parts[0]
        if kind == "TRADE" and len(parts) == 5:
            trades.append(Trade(*(_integer(value, "trade field") for value in parts[1:])))
        elif kind == "EVENT_TRADE" and len(parts) == 5:
            events.append(Event(kind, tuple(_integer(value, "event field") for value in parts[1:])))
        elif kind == "EVENT_LEVEL" and len(parts) == 6:
            if parts[1] not in {"BID", "ASK"} or parts[2] not in {"ADDED", "UPDATED", "REMOVED"}:
                raise ProtocolError("Invalid level event")
            events.append(Event(kind, (parts[1], parts[2], *(_integer(value, "level event") for value in parts[3:]))))
        elif kind == "EVENT_QUOTE" and len(parts) == 3:
            events.append(Event(kind, (_optional_price(parts[1]), _optional_price(parts[2]))))
        elif kind == "QUOTE" and len(parts) == 3:
            if quote is not None:
                raise ProtocolError("Duplicate quote record")
            quote = Quote(_optional_price(parts[1]), _optional_price(parts[2]))
        elif kind == "COUNT" and len(parts) == 2:
            if order_count is not None:
                raise ProtocolError("Duplicate order-count record")
            order_count = _integer(parts[1], "order count")
        elif kind == "LEVEL" and len(parts) == 5:
            if parts[1] not in {"BID", "ASK"}:
                raise ProtocolError("Invalid snapshot side")
            levels.append(Level(parts[1], *(_integer(value, "snapshot level") for value in parts[2:])))
        else:
            raise ProtocolError(f"Unknown or malformed record: {kind}")

    header_reason = header[4] if len(header) == 5 else None
    if status == "REJECTED" and not header_reason:
        raise ProtocolError("Rejected response is missing its reason")
    if operation == "SNAPSHOT" and (quote is None or order_count is None):
        raise ProtocolError("Snapshot response is missing summary records")
    bid_prices = [level.price for level in levels if level.side == "BID"]
    ask_prices = [level.price for level in levels if level.side == "ASK"]
    if bid_prices != sorted(bid_prices, reverse=True) or ask_prices != sorted(ask_prices):
        raise ProtocolError("Snapshot levels are out of price order")
    return EngineResponse(status, operation, header_reason, tuple(trades), tuple(events),
                          tuple(levels), quote, order_count)


def format_price_ticks(price: int | None) -> str:
    return "—" if price is None else f"{price:,} ticks"


def format_quantity(quantity: int | None) -> str:
    return "—" if quantity is None else f"{quantity:,}"


@dataclass
class TradeHistory:
    limit: int = 200
    next_sequence: int = 1
    rows: list[dict[str, int]] = field(default_factory=list)

    def extend(self, trades: Sequence[Trade]) -> None:
        for trade in trades:
            self.rows.append({"sequence": self.next_sequence,
                              "price": trade.price,
                              "quantity": trade.quantity,
                              "buy_order_id": trade.buy_order_id,
                              "sell_order_id": trade.sell_order_id})
            self.next_sequence += 1
        self.rows = self.rows[-self.limit :]
