#include "router.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace vrouter {

// ---- Construction / lookup helpers -----------------------------------------

std::ostream& Router::default_explain_stream() {
    return std::cout;
}

std::shared_ptr<Interface>
Router::find_interface_locked(const std::string& name) const {
    auto it = interfaces_.find(name);
    return it == interfaces_.end() ? nullptr : it->second;
}

std::shared_ptr<Interface>
Router::resolve_next_hop_locked(IPv4Addr next_hop) const {
    // LPM over connected routes only. Connected routes always have a
    // populated egress, so resolution is single-level.
    std::shared_ptr<Interface> best;
    int best_len = -1;
    for (const auto& r : routes_) {
        if (!r.is_connected)                   continue;
        if (!r.prefix.contains(next_hop))      continue;
        if (r.prefix.prefix_len() <= best_len) continue;

        best     = r.egress;
        best_len = r.prefix.prefix_len();
    }
    return best;
}

// ---- Mutators --------------------------------------------------------------

void Router::add_interface(std::shared_ptr<Interface> iface) {
    if (!iface) {
        throw std::invalid_argument("add_interface: null interface");
    }

    std::unique_lock lk(mu_);

    if (interfaces_.count(iface->name)) {
        throw std::invalid_argument("duplicate interface name: '" + iface->name + "'");
    }

    const std::string name = iface->name;
    interfaces_.emplace(name, iface);

    // Synthesize the connected route. The connected network is the
    // interface's IP masked by its prefix length.
    Route connected{
        IPv4Prefix{iface->ip.network(iface->prefix_len), iface->prefix_len},
        std::nullopt,        // no next hop — directly attached
        std::move(iface),    // egress is the interface itself
        0,                   // connected routes get preference 0
        true                 // is_connected
    };
    routes_.push_back(std::move(connected));
}

void Router::add_route(Route& route) {
    // Validate: at least one of next_hop / egress must be set.
    if (!route.next_hop && !route.egress) {
        throw std::invalid_argument(
            "route must have either next_hop or egress");
    }

    std::unique_lock lk(mu_);

    // Resolve egress from next_hop if needed.
    if (!route.egress) {
        route.egress = resolve_next_hop_locked(*route.next_hop);
        if (!route.egress) {
            throw std::invalid_argument(
                "cannot resolve next-hop " + route.next_hop->to_string()
                + " for route " + route.prefix.to_string());
        }
    }

    routes_.push_back(route);  // copy into vector
}

bool Router::set_admin_state(const std::string& name, bool up) {
    std::unique_lock lk(mu_);
    auto iface = find_interface_locked(name);
    if (!iface) return false;
    iface->admin_up = up;
    return true;
}

bool Router::set_oper_state(const std::string& name, bool up) {
    std::unique_lock lk(mu_);
    auto iface = find_interface_locked(name);
    if (!iface) return false;
    iface->oper_up = up;
    return true;
}

// ---- Readers ---------------------------------------------------------------

std::shared_ptr<Interface>
Router::find_interface(const std::string& name) const {
    std::shared_lock lk(mu_);
    return find_interface_locked(name);
}

std::vector<Interface> Router::snapshot_interfaces() const {
    std::shared_lock lk(mu_);
    std::vector<Interface> out;
    out.reserve(interfaces_.size());
    for (const auto& [_, p] : interfaces_) {
        out.push_back(*p);  // value copy, including current state bits
    }
    return out;
}

std::vector<Route> Router::snapshot_routes() const {
    std::shared_lock lk(mu_);
    return routes_;  // vector copy
}

std::size_t Router::interface_count() const {
    std::shared_lock lk(mu_);
    return interfaces_.size();
}

std::size_t Router::route_count() const {
    std::shared_lock lk(mu_);
    return routes_.size();
}

// ---- The lookup algorithm --------------------------------------------------

std::optional<Route> Router::lookup(IPv4Addr dst,
                                    bool explain,
                                    std::ostream& os) const {
    std::shared_lock lk(mu_);

    if (explain) {
        os << "Looking up " << dst.to_string() << " against "
           << routes_.size() << " route(s):\n";
    }

    const Route* best = nullptr;
    auto is_better = [&](const Route& r) {
        return best == nullptr ||
               r.prefix.prefix_len() >  best->prefix.prefix_len() ||
              (r.prefix.prefix_len() == best->prefix.prefix_len() &&
               r.preference          <  best->preference);
    };

    for (const auto& r : routes_) {
        const bool match  = r.prefix.contains(dst);
        const bool usable = r.egress && r.egress->is_usable();

        if (explain) {
            os << "  " << r.prefix.to_string()
               << " via " << (r.next_hop ? r.next_hop->to_string()
                                         : std::string{"(connected)"})
               << " dev "  << (r.egress ? r.egress->name : std::string{"<none>"})
               << " pref " << r.preference << " -> ";

            if (!match)             { os << "no match\n"; continue; }
            if (!usable)            { os << "skipped (interface not usable)\n"; continue; }
            if (!is_better(r))      { os << "match, but not better than current best\n"; continue; }

            os << "match, taking it\n";
            best = &r;
        } else {
            if (match && usable && is_better(r)) {
                best = &r;
            }
        }
    }

    if (explain) {
        os << "Result: ";
        if (best) {
            os << best->prefix.to_string()
               << " via " << (best->next_hop ? best->next_hop->to_string()
                                             : std::string{"(connected)"})
               << " dev "  << best->egress->name << "\n";
        } else {
            os << "no route\n";
        }
    }

    return best ? std::optional<Route>{*best} : std::nullopt;
}

}  // namespace vrouter
