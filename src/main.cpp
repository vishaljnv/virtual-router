// src/main.cpp
//
// vrouter — virtual IPv4 router (PoC)
//
// Process layout:
//   - Main thread: loads config, spawns event listener, runs CLI loop.
//   - Event thread: epolls on a named FIFO + shutdown eventfd, applies
//     interface state changes to the shared Router.
//
// Shared state lives in a single Router instance, synchronized with a
// std::shared_mutex inside Router.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "cli.h"
#include "config.h"
#include "events.h"
#include "router.h"

namespace {

struct CliArgs {
    std::string interfaces_path    = "test/sample_data/interfaces.json";
    std::string static_routes_path = "test/sample_data/static_routes.json";
    std::string fifo_path          = "/tmp/vrouter.events";
};

std::optional<CliArgs> parse_args(int argc, char** argv);
void print_usage(std::ostream& os, const char* prog);

}  // namespace

int main(int argc, char** argv) {
    auto args_opt = parse_args(argc, argv);
    if (!args_opt) {
        return 1;
    }
    const auto& args = *args_opt;

    vrouter::Router router;

    // ---- Startup: load config ------------------------------------------------
    try {
        vrouter::load_interfaces(router, args.interfaces_path);
        vrouter::load_static_routes(router, args.static_routes_path);
    } catch (const std::exception& e) {
        std::cerr << "vrouter: config error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "vrouter: loaded " << router.interface_count()
              << " interfaces, " << router.route_count() << " routes\n";

    // ---- Start event listener ------------------------------------------------
    vrouter::EventListener events(router, args.fifo_path);
    try {
        events.start();
    } catch (const std::exception& e) {
        std::cerr << "vrouter: failed to start event listener: " << e.what() << "\n";
        return 1;
    }
    std::cout << "vrouter: listening for events on " << args.fifo_path << "\n";

    // ---- CLI loop (blocks until 'quit') --------------------------------------
    vrouter::run_cli(router);

    // ---- Shutdown ------------------------------------------------------------
    events.stop();
    return 0;
}

namespace {

std::optional<CliArgs> parse_args(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(std::cout, argv[0]);
            return std::nullopt;
        } else if ((a == "-f" || a == "--fifo") && i + 1 < argc) {
            args.fifo_path = argv[++i];
        } else if (a == "--interfaces" && i + 1 < argc) {
            args.interfaces_path = argv[++i];
        } else if (a == "--routes" && i + 1 < argc) {
            args.static_routes_path = argv[++i];
        } else {
            std::cerr << "vrouter: unknown argument: " << a << "\n";
            print_usage(std::cerr, argv[0]);
            return std::nullopt;
        }
    }
    return args;
}

void print_usage(std::ostream& os, const char* prog) {
    os << "Usage: " << prog << " [options]\n"
       << "Options:\n"
       << "  -f, --fifo PATH        FIFO path for async events"
                                  " (default: /tmp/vrouter.events)\n"
       << "  --interfaces PATH      Path to interfaces.json"
                                  " (default: test/sample_data/interfaces.json)\n"
       << "  --routes PATH          Path to static_routes.json"
                                  " (default: test/sample_data/static_routes.json)\n"
       << "  -h, --help             Show this help\n";
}

}  // namespace
