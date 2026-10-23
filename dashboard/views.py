from __future__ import annotations

from collections.abc import Sequence

from dashboard.data_models import Level


def ladder_table(levels: Sequence[Level], side: str, depth: int, best: int | None) -> str:
    selected = [level for level in levels if level.side == side][:depth]
    if not selected:
        return '<div class="empty-book">No resting orders</div>'
    maximum = max(level.quantity for level in selected) or 1
    color = "#34D399" if side == "BID" else "#F87171"
    price_class = "bid-price" if side == "BID" else "ask-price"
    rows = []
    for level in selected:
        width = max(2, round(100 * level.quantity / maximum))
        label = f"<strong>{level.price:,}</strong>" if level.price == best else f"{level.price:,}"
        rows.append(
            f"<tr><td class='{price_class}'>{label}</td>"
            f"<td>{level.quantity:,}</td><td>{level.order_count:,}</td>"
            f"<td class='bar-cell'><div class='bar' style='width:{width}%;background:{color}'></div></td></tr>"
        )
    return (
        '<table class="ladder"><thead><tr><th>Price (ticks)</th><th>Qty</th>'
        '<th>Orders</th><th></th></tr></thead><tbody>' + "".join(rows) + "</tbody></table>"
    )
