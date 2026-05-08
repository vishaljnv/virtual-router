# vrouter — Design

## Overview

`vrouter` is a single Linux process with two threads:

- **Main thread**: loads config, then runs the interactive CLI loop.
- **Event thread**: blocks in `epoll_wait` on a named FIFO and a
  shutdown `eventfd`, applying interface state changes from external
  producers.

All shared state lives in a single `Router` instance. Concurrent access
is mediated by a `std::shared_mutex` inside the Router: readers (CLI
lookups, snapshots for `show` commands) take a shared lock; writers
(state changes from CLI or FIFO) take a unique lock.

```
                ┌──────────────────────────┐
                │         Router           │
                │  (shared_mutex-guarded)  │
                │                          │
                │  interfaces_  routes_    │
                └────────▲─────────▲───────┘
                         │ writes  │ reads
       ┌──────────┐      │         │      ┌──────────┐
       │  Event   │──────┘         └──────│   CLI    │
       │ thread   │                       │  thread  │
       │ (epoll)  │                       │ (stdin)  │
       └────┬─────┘                       └──────────┘
            │
       ┌────▼─────┐
       │  FIFO    │  echo "eth0,DOWN" > /tmp/vrouter.events
       └──────────┘
```

## Key design decisions

### Two threads, one lock

The CLI and event handler need to coordinate on shared state, but the
write rate is low (a few events per second at most) and the read rate
is human-paced. A single `shared_mutex` covering interfaces and routes
together is simpler than splitting locks per container, and avoids any
lock-ordering rules. The cost (one atomic on every CLI lookup) is
invisible at this scale.

### Routes are immutable after startup

`add_route` is called once per route during config load and never
again. At runtime, the `routes_` vector is treated as read-only — only
`Interface` state bits (`admin_up`, `oper_up`) mutate. This invariant
is what lets `lookup()` walk the table without any per-route
synchronization beyond the shared lock acquired at the top.

If runtime route mutation is ever added (see Future Work), this
invariant must be revisited.

### Routes hold `shared_ptr<Interface>` to their egress

Originally we considered storing interface name as a string in `Route`
and looking up the interface at lookup time. Using `shared_ptr`
eliminates that lookup, makes the dependency explicit, and stays valid
across any potential map-rehashing because the heap allocation never
moves. `is_usable()` is a direct pointer dereference on the hot path.

### Egress resolution at startup, not at runtime

A static route may be specified with `next_hop_ip` only,
`egress_interface` only, or both. When only `next_hop_ip` is given,
the Router resolves the egress interface at `add_route()` time by
running an LPM walk over connected routes. This means runtime
`lookup()` always finds a populated egress field and never needs to
recurse.

If an interface goes down, routes that resolved through it remain in
the table but are filtered out at lookup time via `egress->is_usable()`.

### Linear-scan LPM

For a few hundred routes, walking the vector and tracking the best
match is faster (and dramatically simpler) than a trie or hashed
prefix lookup. The lookup loop is short and branch-friendly. Trie or
DXR-style structures would be the natural upgrade path if route counts
grow into the thousands.

### Admin state vs oper state

`Interface` carries two bools. Admin state is operator-configured (CLI
`shutdown` / `no shutdown`); oper state reflects the link
(FIFO-driven). An interface is usable only when both are up. This
matches real-world router semantics and lets the CLI and event paths
operate on disjoint fields without conflicting.

### Named FIFO + eventfd over plain `pipe(2)`

Three reasons:

1. The spec requires external producers (`echo "..." > /tmp/...`) to
   inject events. That mandates a filesystem path, which means a named
   FIFO.
2. Opening the FIFO `O_RDWR` keeps a writer reference alive inside our
   own process, which suppresses the EOF-storm that
   `epoll`+`O_RDONLY` would otherwise produce when external writers
   come and go.
3. A second fd in the epoll set — an `eventfd` — gives us a clean
   shutdown signal without needing thread cancellation primitives.

### Config loader has no domain knowledge

The loader's job is to translate JSON into Router method calls and
nothing else. Synthesizing connected routes, resolving next-hops, and
validating the routing table are all Router responsibilities. This
keeps the JSON schema and the routing semantics separately
maintainable.

### Generic config errors, not per-field validation

Any failure during config load — file not found, JSON syntax error,
missing field, type mismatch, unresolvable next-hop — surfaces as a
single `ConfigError` that aborts startup. The user's path forward is
to fix the file and re-run. Per-field semantic validation
("`prefix_len` must be 0–32") is deferred to Future Work.

## Future Work

1. **Per-field semantic config validation** — currently the loader
   wraps any underlying parse error into a generic `ConfigError`. A
   richer schema validator with line/column reporting would help users
   diagnose bad configs.
2. **Runtime interface add/remove and route mutation** — would require
   revisiting the routes-immutable invariant and the single-lock
   geometry.
3. **SIGINT handler** — currently Ctrl-C leaves a stale FIFO behind
   that gets cleaned up at next startup. A signal handler would let us
   `unlink()` cleanly on exit.
4. **Readline integration** — async event log lines on stderr currently
   interleave with the CLI prompt. Readline would let us redraw the
   prompt below async output.
5. **Event coalescing** — repeated up/down for the same interface in
   quick succession all apply individually. A small coalescing window
   would smooth flapping.
6. **Logging framework** — currently `std::cerr` with a `[event]`
   prefix. A real logger with severity levels and timestamps is
   straightforward to retrofit.
7. **Recursive next-hop resolution at runtime** — currently next-hops
   are resolved once at load time against connected routes. A more
   general system would resolve through other static routes, possibly
   re-resolving when interfaces flap.
8. **Tighter FIFO permissions** — currently `0666` for ease of use in a
   single-user PoC. Production systems would want this configurable.
9. **Trie-based LPM** — linear scan is fine for hundreds of routes; a
   binary trie or DIR-24-8 structure would be the natural upgrade for
   tens of thousands.
10. **Direct unit tests for the config loader** — currently covered
    transitively by integration tests. Targeted unit tests with
    crafted bad-input fixtures would tighten the feedback loop on
    loader regressions.
