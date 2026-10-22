# Design Notes

## Matching and priority

The order book is continuous and price-time prioritized. A buy walks the lowest ask prices up to its limit; a sell walks the highest bid prices down to its limit. Within one price level, the front order is always matched first. The execution price comes from that resting order. Each match consumes the smaller of the incoming and resting quantities. Fully consumed resting orders leave both the FIFO queue and ID index; a partially consumed order remains at the queue front with reduced quantity. Any incoming remainder is added at the end of its own side's FIFO queue.

## Data structures and costs

- Bids use `std::map<Price, PriceLevel, std::greater<Price>>`; asks use `std::map<Price, PriceLevel, std::less<Price>>`. The best level is `begin()`, and price insertion, lookup, and erase are O(log L) for L levels.
- Each `PriceLevel` stores a `std::list<Order>` and total quantity. Queue append/front access and erase by iterator are O(1); list nodes have allocation and locality costs.
- `std::unordered_map<OrderId, OrderLocation>` stores side, price, and a list iterator. ID lookup/removal is expected O(1), subject to hash-table behavior. The located price level is found in O(log L), then the order is erased by iterator in O(1). Overall cancellation is expected O(1) + O(log L).
- Resting insertion performs an aggregate lookup and price-level insertion (O(log L)), FIFO append (O(1)), and expected O(1) ID-index insertion.
- A non-crossing `submit_order` checks the opposing best level, then inserts. A crossing submission processes each trade in O(1) order work, with O(log L) map erase for each emptied level, and may insert a remainder. With T trades and K emptied price levels, matching is approximately O(T + K log L), plus final insertion if needed.
- Best-price access is O(1). A depth snapshot of D levels takes O(D) traversal and copies its result into owned vectors. Per-price aggregate query is O(log L).
- `submit_order_with_events` first scans eligible opposing levels to capture their previous states, then runs ordinary matching and builds ordered event results. That extra pass and allocations explain its lower measured throughput. The plain `submit_order` path does not build market-data events.

These are container-level complexity estimates, not worst-case latency guarantees. Hash collisions, allocations, exceptions, and operating-system scheduling affect observed cost.

## Invariants and behavior

- Each resting order is in exactly one side/price FIFO and has one matching ID-index entry.
- Each level's `total_quantity` equals the sum of quantities in its FIFO; empty levels are erased.
- Bid/ask maps keep the best level at `begin()`.
- IDs are rejected if zero, invalid order fields are rejected, and duplicate IDs are rejected while resting. Fully filled and cancelled IDs can be reused.
- `depth_snapshot` returns detached value data. Events distinguish trades from level changes and quote changes. For one submission, event order is execution-ordered trades, changed levels, then the changed best quote.

Tests cover core order/match/cancel behavior, fixed-seed randomized add/cancel invariants, deterministic event and snapshot scenarios, aggregate overflow rejection through the simulator, and the ID-reuse policy. They do not yet provide a randomized independent matching oracle or allocation-failure injection.

## Exception, overflow, and concurrency tradeoffs

Resting insertion checks aggregate overflow and rolls back its new list node if index insertion throws. The event-producing simulator path preflights the possible resting remainder against aggregate capacity. The legacy `OrderBook::submit_order` still returns only a trade vector; an empty vector can mean a rejected order or a valid non-crossing submission. Matching is not transactionally rolled back if later allocations throw. In particular, result construction after a mutation can throw before the caller receives all resulting trades/events.

The class has no synchronization. Concurrent reads and writes, concurrent submissions, or concurrent cancellations are unsupported. There is no persistence, sequence recovery, replay log, network feed, or exchange protocol integration.

## Benchmarks

`benchmarks/benchmark.cpp` includes single-price and event-enabled matching, multiple price levels, partial fills/multiple resting orders, cancellation, non-crossing accumulation, quote/quantity queries, and mixed API operations. `cancellation_benchmark.cpp` populates 500,000 orders before timing their cancellation. Timings use `steady_clock`; the harness reports operation counts and validates ending state. The cancellation benchmark excludes setup; most other workload setup is part of the timed scenario unless the output/docs say otherwise. The latency test measures one buy submission against an ask set up outside each sample and includes per-sample clock-read overhead.

The workloads are deterministic synthetic loops and do not model production message distributions, contention, NUMA, network ingress, or tail-latency guarantees. The tested WSL host did not expose hardware counters to `perf`, and benchmark runs varied substantially. See the README for the measured machine, workload definitions, results, and reproduction commands.

## Alternatives considered

- A flat sorted vector of price levels could improve locality for shallow books, but insertion/erase may shift many elements; current cancellation and matching behavior benefits from map lookup and stable levels.
- `std::deque` or an intrusive queue could reduce some list allocation costs, but arbitrary erase by stored iterator and iterator stability require careful design. No allocator or queue replacement is justified without representative measurements.
- A flat hash map may improve ID-index locality, but would add a dependency or implementation complexity. The standard unordered map is retained for portability.
- A direct callback/event sink could avoid constructing returned vectors, but would introduce callback reentrancy and error/ownership choices. The current synchronous value result is simpler and explicit.

## Interview preparation

**Why an ordered map for prices?** It keeps prices sorted and exposes the best bid/ask directly at `begin()`. Updates are O(log L). A flat structure may be faster for very small fixed-depth books but has more costly insertions and erases.

**Why FIFO queues at each price?** The queue encodes time priority directly: matching consumes the front and a new resting order appends at the back.

**How does cancellation find an order?** The ID hash index points to its side, price, and list iterator. The engine looks up the price level and erases that exact node without traversing the FIFO.

**What are the operation costs?** Insertion is O(log L) plus expected O(1) hash insertion. Cancellation is expected O(1) ID lookup + O(log L) level lookup + O(1) list erase. Matching is proportional to the number of executions, with O(log L) cost when an empty price level is erased. Best quote reads are O(1). These ignore allocation and worst-case hash behavior.

**How do allocation and locality affect latency?** Map, list, and hash nodes are separately allocated and can miss cache; per-trade vector growth can allocate too. A pool or flat structure could help, but should be evaluated with allocation profiling and realistic workloads before replacing simple containers.

**How would you make a deterministic market-data feed?** Assign a monotonically increasing sequence at the single matching point, serialize typed trade/book events in the documented order, and persist or replay the exact input sequence. Recovery would need sequence checkpoints and a defined snapshot/event boundary; the current code does not implement these.

**What is needed for concurrency or production use?** Define ownership and sequencing, synchronization or a single-writer architecture, transaction/error guarantees, bounded queues and backpressure, persistence/recovery, protocol validation, operational monitoring, and extensive fault and load testing. The current simulator supplies none of those guarantees.
