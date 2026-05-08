#include "config.h"

#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "interface.h"
#include "ip.h"
#include "route.h"
#include "router.h"

namespace vrouter {

namespace {

[[noreturn]] void rethrow_as_config_error(const std::string& path,
                                          const std::exception& e) {
    throw ConfigError("failed to load '" + path + "': " + e.what());
}

nlohmann::json read_json_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw ConfigError("cannot open '" + path + "'");
    }
    return nlohmann::json::parse(ifs);
}

}  // namespace

void load_interfaces(Router& router, const std::string& path) {
    try {
        auto j = read_json_file(path);

        for (const auto& item : j) {
            auto iface = std::make_shared<Interface>();
            iface->name = item.at("name").get<std::string>();

            const auto ip_prefix_str = item.at("ip_prefix").get<std::string>();
            auto [addr, len] = parse_ip_with_prefix(ip_prefix_str);
            iface->ip         = addr;
            iface->prefix_len = len;

            iface->admin_up = parse_state(item.at("admin_state").get<std::string>());
            iface->oper_up  = parse_state(item.at("oper_state").get<std::string>());

            // Stats — load if present, default to 0 otherwise.
            iface->rx_packets = item.value("rx_packets", uint64_t{0});
            iface->tx_packets = item.value("tx_packets", uint64_t{0});
            iface->rx_bytes   = item.value("rx_bytes",   uint64_t{0});
            iface->tx_bytes   = item.value("tx_bytes",   uint64_t{0});

            // Router synthesizes the connected route internally.
            router.add_interface(iface);
        }
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception& e) {
        rethrow_as_config_error(path, e);
    }
}

void load_static_routes(Router& router, const std::string& path) {
    try {
        auto j = read_json_file(path);

        for (const auto& item : j) {
            Route r;
            r.prefix     = IPv4Prefix::from_cidr(item.at("prefix").get<std::string>());
            r.preference = item.value("preference", uint32_t{1});

            const bool has_nh    = item.contains("next_hop_ip");
            const bool has_egr   = item.contains("egress_interface");

            if (!has_nh && !has_egr) {
                throw ConfigError(
                    "route " + r.prefix.to_string()
                    + " must have next_hop_ip or egress_interface");
            }

            if (has_nh) {
                r.next_hop = IPv4Addr::from_string(
                    item.at("next_hop_ip").get<std::string>());
            }

            if (has_egr) {
                const auto iface_name = item.at("egress_interface").get<std::string>();
                auto iface = router.find_interface(iface_name);
                if (!iface) {
                    throw ConfigError(
                        "route " + r.prefix.to_string()
                        + " references unknown interface '" + iface_name + "'");
                }
                r.egress = iface;
            }

            // Router resolves egress from next_hop if egress is null.
            router.add_route(r);
        }
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception& e) {
        rethrow_as_config_error(path, e);
    }
}

}  // namespace vrouter
