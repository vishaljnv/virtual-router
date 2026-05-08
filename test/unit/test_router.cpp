#include <catch2/catch.hpp>

#include <memory>
#include <sstream>

#include "interface.h"
#include "ip.h"
#include "route.h"
#include "router.h"

using namespace vrouter;

namespace {

std::shared_ptr<Interface> make_iface(const std::string& name,
                                      const std::string& ip,
                                      uint8_t prefix_len,
                                      bool admin_up = true,
                                      bool oper_up  = true) {
    auto i = std::make_shared<Interface>();
    i->name       = name;
    i->ip         = IPv4Addr::from_string(ip);
    i->prefix_len = prefix_len;
    i->admin_up   = admin_up;
    i->oper_up    = oper_up;
    return i;
}

}  // namespace

TEST_CASE("add_interface synthesizes connected route", "[router]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 24));

    REQUIRE(r.interface_count() == 1);
    REQUIRE(r.route_count()     == 1);

    auto routes = r.snapshot_routes();
    REQUIRE(routes[0].is_connected);
    REQUIRE(routes[0].prefix.to_string() == "10.0.0.0/24");
    REQUIRE(routes[0].preference         == 0);
    REQUIRE(routes[0].egress->name       == "eth0");
}

TEST_CASE("add_interface rejects duplicate names", "[router]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 24));
    REQUIRE_THROWS_AS(
        r.add_interface(make_iface("eth0", "192.168.1.1", 24)),
        std::invalid_argument);
}

TEST_CASE("add_route resolves egress from next_hop", "[router]") {
    Router r;
    r.add_interface(make_iface("eth0", "192.168.1.1", 24));
    r.add_interface(make_iface("eth1", "10.0.0.1",    16));

    Route route;
    route.prefix     = IPv4Prefix::from_cidr("172.16.0.0/16");
    route.next_hop   = IPv4Addr::from_string("10.0.0.2");  // in eth1's network
    route.preference = 1;

    r.add_route(route);

    REQUIRE(route.egress != nullptr);
    REQUIRE(route.egress->name == "eth1");
}

TEST_CASE("add_route fails when next_hop is unreachable", "[router]") {
    Router r;
    r.add_interface(make_iface("eth0", "192.168.1.1", 24));

    Route route;
    route.prefix   = IPv4Prefix::from_cidr("172.16.0.0/16");
    route.next_hop = IPv4Addr::from_string("8.8.8.8");  // not in any connected net

    REQUIRE_THROWS_AS(r.add_route(route), std::invalid_argument);
}

TEST_CASE("lookup: longest prefix match wins", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 8));   // 10.0.0.0/8 connected
    r.add_interface(make_iface("eth1", "10.1.0.1", 16));  // 10.1.0.0/16 connected

    auto result = r.lookup(IPv4Addr::from_string("10.1.5.5"));
    REQUIRE(result.has_value());
    REQUIRE(result->prefix.to_string() == "10.1.0.0/16");
    REQUIRE(result->egress->name       == "eth1");
}

TEST_CASE("lookup: tie-break on preference", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "192.168.1.1", 24));
    r.add_interface(make_iface("eth1", "10.0.0.1",    16));

    Route a;
    a.prefix     = IPv4Prefix::from_cidr("172.16.0.0/16");
    a.next_hop   = IPv4Addr::from_string("192.168.1.254");
    a.preference = 20;
    r.add_route(a);

    Route b;
    b.prefix     = IPv4Prefix::from_cidr("172.16.0.0/16");
    b.next_hop   = IPv4Addr::from_string("10.0.0.2");
    b.preference = 10;  // lower preference wins
    r.add_route(b);

    auto result = r.lookup(IPv4Addr::from_string("172.16.5.5"));
    REQUIRE(result.has_value());
    REQUIRE(result->preference   == 10);
    REQUIRE(result->egress->name == "eth1");
}

TEST_CASE("lookup: unusable interface skipped", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 8));   // /8 connected
    r.add_interface(make_iface("eth1", "10.1.0.1", 16));  // /16 connected

    // /16 wins normally; bring eth1 oper down and the /8 should be chosen.
    r.set_oper_state("eth1", false);

    auto result = r.lookup(IPv4Addr::from_string("10.1.5.5"));
    REQUIRE(result.has_value());
    REQUIRE(result->prefix.to_string() == "10.0.0.0/8");
    REQUIRE(result->egress->name       == "eth0");
}

TEST_CASE("lookup: admin_down also disqualifies", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 8));
    r.add_interface(make_iface("eth1", "10.1.0.1", 16));

    r.set_admin_state("eth1", false);

    auto result = r.lookup(IPv4Addr::from_string("10.1.5.5"));
    REQUIRE(result.has_value());
    REQUIRE(result->egress->name == "eth0");
}

TEST_CASE("lookup: no match returns nullopt", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 24));

    auto result = r.lookup(IPv4Addr::from_string("8.8.8.8"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("lookup: default route as fallback", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "192.168.1.1", 24));

    Route def;
    def.prefix     = IPv4Prefix::from_cidr("0.0.0.0/0");
    def.next_hop   = IPv4Addr::from_string("192.168.1.254");
    def.preference = 1;
    r.add_route(def);

    auto result = r.lookup(IPv4Addr::from_string("8.8.8.8"));
    REQUIRE(result.has_value());
    REQUIRE(result->prefix.to_string() == "0.0.0.0/0");
}

TEST_CASE("explain output describes the algorithm", "[router][lookup]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 8));
    r.add_interface(make_iface("eth1", "10.1.0.1", 16));

    std::ostringstream os;
    auto result = r.lookup(IPv4Addr::from_string("10.1.5.5"),
                           /*explain=*/true, os);

    const std::string out = os.str();
    REQUIRE(result.has_value());

    // Spot-check that key bits of the narration are present.
    REQUIRE(out.find("Looking up 10.1.5.5") != std::string::npos);
    REQUIRE(out.find("10.1.0.0/16")         != std::string::npos);
    REQUIRE(out.find("Result:")             != std::string::npos);
}

TEST_CASE("set_oper_state and set_admin_state on missing iface", "[router]") {
    Router r;
    r.add_interface(make_iface("eth0", "10.0.0.1", 24));

    REQUIRE(r.set_oper_state("eth0",  false));
    REQUIRE(r.set_admin_state("eth0", false));
    REQUIRE_FALSE(r.set_oper_state("eth9",  true));
    REQUIRE_FALSE(r.set_admin_state("eth9", true));
}
