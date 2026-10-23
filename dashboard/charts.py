from __future__ import annotations

from collections.abc import Sequence

import plotly.graph_objects as go

from dashboard.data_models import Level, Quote


def cumulative_points(levels: Sequence[Level], side: str) -> tuple[list[int], list[int], list[int]]:
    selected = [level for level in levels if level.side == side]
    selected.sort(key=lambda level: level.price, reverse=(side == "BID"))
    points: list[tuple[int, int, int]] = []
    cumulative = 0
    for level in selected:
        cumulative += level.quantity
        points.append((level.price, cumulative, level.quantity))
    if side == "BID":
        points.reverse()
    return ([point[0] for point in points],
            [point[1] for point in points],
            [point[2] for point in points])


def depth_figure(levels: Sequence[Level], quote: Quote | None, depth: int) -> go.Figure:
    visible = ([level for level in levels if level.side == "BID"][:depth]
               + [level for level in levels if level.side == "ASK"][:depth])
    bid_x, bid_cumulative, bid_level_qty = cumulative_points(visible, "BID")
    ask_x, ask_cumulative, ask_level_qty = cumulative_points(visible, "ASK")
    figure = go.Figure()
    if bid_x:
        figure.add_trace(go.Scatter(
            x=bid_x,
            y=bid_cumulative,
            customdata=bid_level_qty,
            mode="lines",
            line={"color": "#34D399", "shape": "hv", "width": 2},
            fill="tozeroy",
            fillcolor="rgba(52, 211, 153, 0.14)",
            name="Bids",
            hovertemplate="Price: %{x} ticks<br>Level qty: %{customdata}<br>Cumulative qty: %{y}<extra>Bids</extra>",
        ))
    if ask_x:
        figure.add_trace(go.Scatter(
            x=ask_x,
            y=ask_cumulative,
            customdata=ask_level_qty,
            mode="lines",
            line={"color": "#F87171", "shape": "hv", "width": 2},
            fill="tozeroy",
            fillcolor="rgba(248, 113, 113, 0.14)",
            name="Asks",
            hovertemplate="Price: %{x} ticks<br>Level qty: %{customdata}<br>Cumulative qty: %{y}<extra>Asks</extra>",
        ))
    if quote is not None and quote.bid is not None and quote.ask is not None:
        figure.add_vrect(x0=quote.bid, x1=quote.ask, fillcolor="#60A5FA",
                         opacity=0.08, line_width=0, layer="below")
    figure.update_layout(
        template="plotly_dark",
        paper_bgcolor="#111827",
        plot_bgcolor="#111827",
        font={"color": "#E5E7EB"},
        margin={"l": 48, "r": 18, "t": 18, "b": 42},
        height=340,
        legend={"orientation": "h", "y": 1.12, "x": 0},
        xaxis_title="Price (integer ticks)",
        yaxis_title="Cumulative resting quantity",
        hovermode="x unified",
    )
    return figure
