#ifndef VROUTER_ROUTER_H
#define VROUTER_ROUTER_H

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "interface.h"
#include "route.h"

namespace vrouter {

// The Router holds all interfaces and routes, and is the single point of
// synchronization for concurrent access from the CLI thread (reader) and
// the event thread (writer).
//
// Concurrency model: a single std::shared_mutex guards both interfaces_
// and routes_. Mutators take a unique_lock; readers take a shared_lock.
//
// Lifecycle:
//   - Construction: empty.
//   - Startup phase: add_interface() / add_route() called once each, in
//     order. Routes reference interfaces by shared_ptr, so interfaces
//     must be added first.
//   - Steady-state: routes_ is treated as IMMUTABLE. The set of routes
//     and their fields never change after startup. Only Interface state
//     bits (admin_up, oper_up) mutate, via set_admin_state() and
//     set_oper_state(). This invariant is what makes lookup() simple —
//     no need to worry about routes being added or removed mid-walk.
//
//     If runtime route mutation is ever added (Future Work), revisit
//     locking on the lookup path.
class Router {
public:
    Router() = default;

    // Non-copyable, non-movable: holds a mutex and is referenced from
    // multiple threads by address.
    Router(const Router&)            = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&)                 = delete;
    Router& operator=(Router&&)      = delete;

    // ---- Mutators (intended for startup; safe to call concurrently) -----

    // Adds an interface. Also synthesizes a connected route for the
    // interface's network with preference 0. Throws std::invalid_argument
    // if an interface with the same name already exists.
    void add_interface(std::shared_ptr<Interface> iface);

    // Adds a route. If `route.egress` is null but `route.next_hop` is
    // set, the Router resolves egress by finding the longest-prefix
    // CONNECTED route covering next_hop, and copies that route's egress
    // here. If resolution fails, throws std::invalid_argument.
    //
    // After this call returns, route.egress is guaranteed non-null.
    //
    // Intended for startup only. Routes are treated as immutable in
    // steady state (see class comment).
    void add_route(Route& route);

    // ---- Steady-state mutators (concurrent with reads) -----------------

    // CLI 'shutdown' / 'no shutdown'. Returns false if no such interface.
    bool set_admin_state(const std::string& name, bool up);

    // FIFO event handler. Returns false if no such interface.
    bool set_oper_state(const std::string& name, bool up);

    // ---- Readers --------------------------------------------------------

    // Looks up the best matching route for `dst`.
    //
    //  - LPM: longest prefix wins.
    //  - Tie-break: lowest `preference` wins.
    //  - Filtering: only routes whose egress interface is_usable() are
    //    considered.
    //
    // Returns std::nullopt if no usable route matches.
    //
    // If `explain` is true, the algorithm narrates each step to `os`:
    // every candidate route examined, whether it matched, why it was
    // rejected or selected, and the final result.
    std::optional<Route> lookup(IPv4Addr dst,
                                bool explain = false,
                                std::ostream& os = default_explain_stream()) const;

    // Returns nullptr if not found.
    std::shared_ptr<Interface> find_interface(const std::string& name) const;

    // Snapshots: take the lock, copy, release. Callers can format/print
    // without holding the lock.
    std::vector<Interface> snapshot_interfaces() const;
    std::vector<Route>     snapshot_routes() const;

    std::size_t interface_count() const;
    std::size_t route_count() const;

private:
    // Caller must hold mu_ (any mode).
    std::shared_ptr<Interface> find_interface_locked(const std::string& name) const;

    // Resolves egress for a route with only next_hop set. Caller must
    // hold mu_ in unique mode (writer). Returns nullptr on failure.
    std::shared_ptr<Interface> resolve_next_hop_locked(IPv4Addr next_hop) const;

    // Returns std::cout. In a header to avoid pulling <iostream> here;
    // defined in router.cpp.
    static std::ostream& default_explain_stream();

    mutable std::shared_mutex mu_;

    std::unordered_map<std::string, std::shared_ptr<Interface>> interfaces_;

    // INVARIANT: not mutated after startup. See class comment.
    std::vector<Route> routes_;
};

}  // namespace vrouter

#endif  // VROUTER_ROUTER_H
