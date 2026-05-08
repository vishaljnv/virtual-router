#include "ip.h"

#include <arpa/inet.h>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace vrouter {

namespace {

// Returns the network mask for a prefix length.
// Special-cases 0 to avoid UB from shifting a 32-bit int by 32.
uint32_t mask_for(uint8_t prefix_len) {
    return prefix_len == 0 ? 0u : (~0u << (32 - prefix_len));
}

uint32_t parse_dotted(const std::string& s) {
    in_addr a{};
    if (inet_pton(AF_INET, s.c_str(), &a) != 1) {
        throw std::invalid_argument("invalid IPv4 address: '" + s + "'");
    }
    // inet_pton gives network byte order; convert to host order.
    return ntohl(a.s_addr);
}

}  // namespace

// ---- IPv4Addr ---------------------------------------------------------------

IPv4Addr IPv4Addr::from_string(const std::string& s) {
    return IPv4Addr{parse_dotted(s)};
}

std::string IPv4Addr::to_string() const {
    in_addr a{};
    a.s_addr = htonl(value_);
    char buf[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &a, buf, sizeof(buf))) {
        // inet_ntop only fails on programming errors (bad family, undersized
        // buffer). Either way, no useful recovery.
        throw std::runtime_error("inet_ntop failed");
    }
    return buf;
}

IPv4Addr IPv4Addr::network(uint8_t prefix_len) const {
    if (prefix_len > 32) {
        throw std::invalid_argument("prefix length must be <= 32");
    }
    return IPv4Addr{value_ & mask_for(prefix_len)};
}

// ---- IPv4Prefix -------------------------------------------------------------

IPv4Prefix::IPv4Prefix(IPv4Addr network, uint8_t prefix_len)
    : network_{network.network(prefix_len)},  // canonicalize
      prefix_len_{prefix_len}
{
    if (prefix_len > 32) {
        throw std::invalid_argument("prefix length must be <= 32");
    }
}

IPv4Prefix IPv4Prefix::from_cidr(const std::string& s) {
    auto [addr, len] = parse_ip_with_prefix(s);
    return IPv4Prefix{addr, len};  // ctor masks host bits
}

std::string IPv4Prefix::to_string() const {
    return network_.to_string() + "/" + std::to_string(prefix_len_);
}

bool IPv4Prefix::contains(IPv4Addr addr) const {
    return (addr.value() & mask_for(prefix_len_)) == network_.value();
}

// ---- Free functions ---------------------------------------------------------

std::pair<IPv4Addr, uint8_t> parse_ip_with_prefix(const std::string& s) {
    auto slash = s.find('/');
    if (slash == std::string::npos) {
        throw std::invalid_argument("missing '/' in CIDR: '" + s + "'");
    }

    const std::string addr_part   = s.substr(0, slash);
    const std::string prefix_part = s.substr(slash + 1);

    if (addr_part.empty() || prefix_part.empty()) {
        throw std::invalid_argument("malformed CIDR: '" + s + "'");
    }

    // Parse prefix length manually so we can reject things like "10.0.0.0/24x"
    // or "/-1" cleanly.
    char* end = nullptr;
    long len = std::strtol(prefix_part.c_str(), &end, 10);
    if (end == prefix_part.c_str() || *end != '\0' || len < 0 || len > 32) {
        throw std::invalid_argument("invalid prefix length in '" + s + "'");
    }

    return {IPv4Addr{parse_dotted(addr_part)}, static_cast<uint8_t>(len)};
}

}  // namespace vrouter
