#ifndef VROUTER_INTERFACE_H
#define VROUTER_INTERFACE_H

#include <cstdint>
#include <stdexcept>
#include <string>

#include "ip.h"

namespace vrouter {

// Parses "up" / "down" into a bool (true == up). Throws std::invalid_argument
// on any other input. Used by both the config loader and the FIFO event parser.
inline bool parse_state(const std::string& s) {
    if (s == "up")   return true;
    if (s == "down") return false;
    throw std::invalid_argument("invalid interface state: '" + s + "'");
}

inline const char* state_to_string(bool up) {
    return up ? "up" : "down";
}

struct Interface {
    std::string name;            // "eth0"
    IPv4Addr    ip;              // interface address (host bits preserved)
    uint8_t     prefix_len;      // network prefix length for `ip`

    // admin_up: configured by operator (CLI: shutdown / no shutdown).
    // oper_up:  reflects the link's actual state (driven by FIFO events).
    // Interface is usable for forwarding iff both are true.
    bool        admin_up;
    bool        oper_up;

    // Statistics — loaded from config, never mutated at runtime.
    uint64_t    rx_packets = 0;
    uint64_t    tx_packets = 0;
    uint64_t    rx_bytes   = 0;
    uint64_t    tx_bytes   = 0;

    bool is_usable() const {
        return admin_up && oper_up;
    }
};

}  // namespace vrouter

#endif  // VROUTER_INTERFACE_H
