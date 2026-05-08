#include <catch2/catch.hpp>

#include <stdexcept>

#include "ip.h"

using namespace vrouter;

TEST_CASE("IPv4Addr parse and format roundtrip", "[ip]") {
    SECTION("typical address") {
        auto a = IPv4Addr::from_string("192.168.1.1");
        REQUIRE(a.to_string() == "192.168.1.1");
    }
    SECTION("zero address") {
        auto a = IPv4Addr::from_string("0.0.0.0");
        REQUIRE(a.value() == 0u);
        REQUIRE(a.to_string() == "0.0.0.0");
    }
    SECTION("broadcast address") {
        auto a = IPv4Addr::from_string("255.255.255.255");
        REQUIRE(a.value() == 0xFFFFFFFFu);
        REQUIRE(a.to_string() == "255.255.255.255");
    }
}

TEST_CASE("IPv4Addr rejects malformed input", "[ip]") {
    REQUIRE_THROWS_AS(IPv4Addr::from_string(""),               std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Addr::from_string("not.an.ip.addr"), std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Addr::from_string("256.0.0.1"),      std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Addr::from_string("1.2.3"),          std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Addr::from_string("1.2.3.4.5"),      std::invalid_argument);
}

TEST_CASE("IPv4Addr::network masks correctly", "[ip]") {
    auto a = IPv4Addr::from_string("10.1.2.3");
    REQUIRE(a.network(8).to_string()  == "10.0.0.0");
    REQUIRE(a.network(16).to_string() == "10.1.0.0");
    REQUIRE(a.network(24).to_string() == "10.1.2.0");
    REQUIRE(a.network(32).to_string() == "10.1.2.3");
    REQUIRE(a.network(0).to_string()  == "0.0.0.0");   // /0 is the UB-prone case
}

TEST_CASE("IPv4Prefix::from_cidr parses and normalizes", "[ip]") {
    SECTION("already canonical") {
        auto p = IPv4Prefix::from_cidr("10.0.0.0/16");
        REQUIRE(p.network().to_string() == "10.0.0.0");
        REQUIRE(p.prefix_len()          == 16);
    }
    SECTION("non-canonical input is normalized") {
        auto p = IPv4Prefix::from_cidr("10.1.5.0/16");  // host bits set
        REQUIRE(p.network().to_string() == "10.1.0.0");
        REQUIRE(p.prefix_len()          == 16);
    }
    SECTION("default route") {
        auto p = IPv4Prefix::from_cidr("0.0.0.0/0");
        REQUIRE(p.network().value() == 0u);
        REQUIRE(p.prefix_len()      == 0);
    }
    SECTION("host route /32") {
        auto p = IPv4Prefix::from_cidr("192.168.1.1/32");
        REQUIRE(p.network().to_string() == "192.168.1.1");
        REQUIRE(p.prefix_len()          == 32);
    }
}

TEST_CASE("IPv4Prefix::from_cidr rejects malformed input", "[ip]") {
    REQUIRE_THROWS_AS(IPv4Prefix::from_cidr("10.0.0.0"),     std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Prefix::from_cidr("10.0.0.0/"),    std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Prefix::from_cidr("/16"),          std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Prefix::from_cidr("10.0.0.0/33"),  std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Prefix::from_cidr("10.0.0.0/-1"),  std::invalid_argument);
    REQUIRE_THROWS_AS(IPv4Prefix::from_cidr("10.0.0.0/24x"), std::invalid_argument);
}

TEST_CASE("IPv4Prefix::contains", "[ip]") {
    auto p = IPv4Prefix::from_cidr("10.1.0.0/16");
    REQUIRE(p.contains(IPv4Addr::from_string("10.1.0.0")));     // network address
    REQUIRE(p.contains(IPv4Addr::from_string("10.1.0.1")));
    REQUIRE(p.contains(IPv4Addr::from_string("10.1.255.255"))); // broadcast
    REQUIRE_FALSE(p.contains(IPv4Addr::from_string("10.0.255.255")));  // just below
    REQUIRE_FALSE(p.contains(IPv4Addr::from_string("10.2.0.0")));      // just above

    auto def = IPv4Prefix::from_cidr("0.0.0.0/0");
    REQUIRE(def.contains(IPv4Addr::from_string("1.2.3.4")));
    REQUIRE(def.contains(IPv4Addr::from_string("0.0.0.0")));
}

TEST_CASE("parse_ip_with_prefix preserves host bits", "[ip]") {
    auto [addr, len] = parse_ip_with_prefix("10.0.0.1/24");
    REQUIRE(addr.to_string() == "10.0.0.1");
    REQUIRE(len              == 24);
}
