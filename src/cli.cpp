#include "cli.h"

#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "interface.h"
#include "ip.h"
#include "route.h"
#include "router.h"

namespace vrouter {

namespace {

constexpr const char* kPrompt = "vrouter:> ";

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> toks;
    std::istringstream iss(line);
    std::string t;
    while (iss >> t) toks.push_back(std::move(t));
    return toks;
}

void cmd_show_interfaces(Router& router) {
    auto ifs = router.snapshot_interfaces();

    std::cout << "NAME      IP/PREFIX             ADMIN  OPER   "
                 "RX_PKTS    TX_PKTS    RX_BYTES   TX_BYTES\n";
    for (const auto& i : ifs) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%-9s %-21s %-6s %-6s %-10llu %-10llu %-10llu %-10llu\n",
            i.name.c_str(),
            (i.ip.to_string() + "/" + std::to_string(i.prefix_len)).c_str(),
            state_to_string(i.admin_up),
            state_to_string(i.oper_up),
            (unsigned long long)i.rx_packets,
            (unsigned long long)i.tx_packets,
            (unsigned long long)i.rx_bytes,
            (unsigned long long)i.tx_bytes);
        std::cout << buf;
    }
}

void cmd_show_routes(Router& router) {
    auto routes = router.snapshot_routes();

    std::cout << "PREFIX             NEXT_HOP         EGRESS    PREF  TYPE\n";
    for (const auto& r : routes) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%-18s %-16s %-9s %-5u %s\n",
            r.prefix.to_string().c_str(),
            (r.next_hop ? r.next_hop->to_string()
                        : std::string{"(connected)"}).c_str(),
            (r.egress ? r.egress->name : std::string{"-"}).c_str(),
            r.preference,
            r.is_connected ? "C" : "S");
        std::cout << buf;
    }
}

void cmd_lookup(Router& router, const std::string& ip_str, bool explain) {
    IPv4Addr dst;
    try {
        dst = IPv4Addr::from_string(ip_str);
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << "\n";
        return;
    }

    auto result = router.lookup(dst, explain, std::cout);

    if (!explain) {
        if (result) {
            std::cout << dst.to_string() << " -> "
                      << result->prefix.to_string()
                      << " via " << (result->next_hop
                                     ? result->next_hop->to_string()
                                     : std::string{"(connected)"})
                      << " dev " << result->egress->name << "\n";
        } else {
            std::cout << dst.to_string() << " -> no route\n";
        }
    }
}

void cmd_set_admin(Router& router, const std::string& iface, bool up) {
    if (!router.set_admin_state(iface, up)) {
        std::cout << "error: no such interface '" << iface << "'\n";
        return;
    }
    std::cout << iface << " admin state -> " << state_to_string(up) << "\n";
}

void print_help() {
    std::cout <<
        "Commands:\n"
        "  show interfaces             list all interfaces\n"
        "  show routes                 list all routes\n"
        "  lookup <ip>                 find best route for <ip>\n"
        "  explain-lookup <ip>         lookup with verbose narration\n"
        "  shutdown <iface>            set interface admin state down\n"
        "  no shutdown <iface>         set interface admin state up\n"
        "  help                        this message\n"
        "  quit | exit                 exit\n";
}

// Returns true if the loop should continue, false to exit.
bool dispatch(Router& router, const std::vector<std::string>& tok) {
    if (tok.empty()) return true;

    const std::string& c = tok[0];

    if (c == "quit" || c == "exit") return false;

    if (c == "help") {
        print_help();
    } else if (c == "show" && tok.size() == 2 && tok[1] == "interfaces") {
        cmd_show_interfaces(router);
    } else if (c == "show" && tok.size() == 2 && tok[1] == "routes") {
        cmd_show_routes(router);
    } else if (c == "lookup" && tok.size() == 2) {
        cmd_lookup(router, tok[1], /*explain=*/false);
    } else if (c == "explain-lookup" && tok.size() == 2) {
        cmd_lookup(router, tok[1], /*explain=*/true);
    } else if (c == "shutdown" && tok.size() == 2) {
        cmd_set_admin(router, tok[1], /*up=*/false);
    } else if (c == "no" && tok.size() == 3 && tok[1] == "shutdown") {
        cmd_set_admin(router, tok[2], /*up=*/true);
    } else {
        std::cout << "unknown or malformed command. type 'help'.\n";
    }

    return true;
}

}  // namespace

void run_cli(Router& router) {
    std::string line;
    while (true) {
        std::cout << kPrompt << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";  // tidy up after Ctrl-D
            return;
        }
        auto tok = tokenize(line);
        if (!dispatch(router, tok)) return;
    }
}

}  // namespace vrouter
