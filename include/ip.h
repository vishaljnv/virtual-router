#ifndef VROUTER_IP_H
#define VROUTER_IP_H

#include <cstdint>
#include <string>
#include <utility>

namespace vrouter {

// IPv4 address stored in host byte order. Lightweight value type;
// pass by value freely.
class IPv4Addr {
public:
    constexpr IPv4Addr() = default;
    constexpr explicit IPv4Addr(uint32_t v) : value_(v) {}

    // Parses dotted-quad form ("192.168.1.1"). Throws std::invalid_argument
    // on malformed input.
    static IPv4Addr from_string(const std::string& s);

    std::string to_string() const;

    // Returns this address with the host bits zeroed for the given prefix
    // length. Useful for deriving a network address from an interface IP.
    // prefix_len must be in [0, 32].
    IPv4Addr network(uint8_t prefix_len) const;

    constexpr uint32_t value() const { return value_; }

    friend constexpr bool operator==(IPv4Addr a, IPv4Addr b) { return a.value_ == b.value_; }
    friend constexpr bool operator!=(IPv4Addr a, IPv4Addr b) { return !(a == b); }
    friend constexpr bool operator<(IPv4Addr a, IPv4Addr b)  { return a.value_ <  b.value_; }

private:
    uint32_t value_ = 0;
};

// IPv4 network prefix: a network address plus a prefix length.
// Invariant: host bits of `network` are zero (i.e. network is canonicalized).
class IPv4Prefix {
public:
    constexpr IPv4Prefix() = default;
    IPv4Prefix(IPv4Addr network, uint8_t prefix_len);

    // Parses CIDR form ("10.1.0.0/16"). Address is silently normalized:
    // host bits are masked off. Throws std::invalid_argument on malformed
    // input or prefix_len > 32.
    static IPv4Prefix from_cidr(const std::string& s);

    // CIDR string form: "10.1.0.0/16".
    std::string to_string() const;

    // True if `addr` falls within this prefix.
    bool contains(IPv4Addr addr) const;

    constexpr IPv4Addr network()    const { return network_; }
    constexpr uint8_t  prefix_len() const { return prefix_len_; }

    friend bool operator==(const IPv4Prefix& a, const IPv4Prefix& b) {
        return a.network_ == b.network_ && a.prefix_len_ == b.prefix_len_;
    }

private:
    IPv4Addr network_;
    uint8_t  prefix_len_ = 0;
};

// Parses a CIDR string ("10.0.0.1/24") returning the address as written
// (host bits preserved) and the prefix length. Used by the interface
// loader where the address part is the interface's actual IP, not a
// network address. Throws std::invalid_argument on malformed input.
std::pair<IPv4Addr, uint8_t> parse_ip_with_prefix(const std::string& s);

}  // namespace vrouter

#endif  // VROUTER_IP_H
