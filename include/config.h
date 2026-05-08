#ifndef VROUTER_CONFIG_H
#define VROUTER_CONFIG_H

#include <stdexcept>
#include <string>

namespace vrouter {

class Router;  // fwd decl — keeps router.h out of this header

// Thrown when a config file cannot be loaded for any reason:
//   - file not found / unreadable
//   - JSON syntax error
//   - missing required fields, wrong types
//   - references to unknown interfaces from routes
//   - duplicate interface names
//
// The what() string includes the file path and (when available) the
// underlying parser's error message. Detailed per-field semantic
// validation (e.g. "prefix_len must be 0-32") is Future Work.
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Loads interfaces from `path` (JSON) and adds them to `router`.
// The Router synthesizes connected routes internally.
//
// Must be called BEFORE load_static_routes(): static routes reference
// interfaces by name and need them to exist.
void load_interfaces(Router& router, const std::string& path);

// Loads static routes from `path` (JSON) and adds them to `router`.
// Each route's `egress` field is resolved either directly (if the JSON
// names an interface) or by next-hop lookup (Router does this).
void load_static_routes(Router& router, const std::string& path);

}  // namespace vrouter

#endif  // VROUTER_CONFIG_H
