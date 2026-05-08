#ifndef VROUTER_ROUTE_H
#define VROUTER_ROUTE_H

#include <cstdint>
#include <memory>
#include <optional>

#include "interface.h"
#include "ip.h"

namespace vrouter {

// A single entry in the routing table.
//
// At least one of `next_hop` or `egress` must be set when adding a route
// to the Router. The Router resolves missing egress at add_route() time
// by looking up the next_hop's network among connected routes already
// present in the table. Once installed, every route has a populated
// egress — runtime lookup() relies on this invariant.
//
// `egress` is a shared_ptr into the Router's interfaces map, so the
// interface is guaranteed to outlive any route that references it.
// Lookup checks egress->is_usable() to determine whether the route is
// currently viable.
//
// `is_connected` is set to true ONLY for routes synthesized from an
// interface (by Router::add_interface). Static routes — even those that
// specify only an egress interface and no next-hop — are not connected.
struct Route {
    IPv4Prefix                  prefix;
    std::optional<IPv4Addr>     next_hop;     // nullopt for connected or
                                              // egress-only static routes
    std::shared_ptr<Interface>  egress;       // populated after add_route()
    uint32_t                    preference   = 1;
    bool                        is_connected = false;
};

}  // namespace vrouter

#endif  // VROUTER_ROUTE_H
