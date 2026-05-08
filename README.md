# vrouter

A virtual IPv4 router (proof-of-concept) implementing a Cisco-style CLI
over a longest-prefix-match routing table, with asynchronous interface
state events delivered via a named FIFO.

## Overview

`vrouter` is a single-process Linux user-space program. At startup it
loads interfaces and static routes from JSON config files, then drops
into an interactive CLI. A background thread watches a named FIFO for
external events that flip interface oper state, simulating link
up/down without requiring root privileges or kernel networking.

What it does:

- Loads interfaces and static routes from JSON config files at startup.
- Synthesizes a connected route for every interface.
- Resolves static-route next-hops to egress interfaces at load time.
- Runs an interactive CLI for inspection and control.
- Performs longest-prefix-match (LPM) lookup with preference-based
  tie-breaking. Routes whose egress interface is admin-down or
  oper-down are skipped.
- Receives async interface state changes via a named FIFO, applied by
  a dedicated event thread.

What it does not do:

- It does not forward real packets. There is no kernel integration.
- It does not require root or any special privileges.

## Build

Requirements:

- Linux (uses `epoll`, `eventfd`, and named FIFOs).
- A C++17 compiler (g++ 9+ or clang++ 10+).

`nlohmann/json` and `Catch2` (v2) single-header dependencies are
vendored under `third_party/` — no external setup needed.

Build the binary:

```sh
make            # optimized build (-O2)
make debug      # debug build (-O0 -g) for use with gdb
make clean
```

The output binary is `./vrouter` in the project root.

## Test

```sh
make test               # unit tests (Catch2)
make integration-test   # end-to-end CLI + FIFO scenarios (bash)
```

The unit tests cover the IP types, CIDR parsing, and the Router's
add/lookup logic. The integration tests build the binary and run real
CLI sessions against it, including async events delivered through the
FIFO.

## Usage

### Running

```sh
./vrouter [options]

  -f, --fifo PATH        FIFO path for async events
                         (default: /tmp/vrouter.events)
  --interfaces PATH      Path to interfaces JSON
                         (default: test/sample_data/interfaces.json)
  --routes PATH          Path to static routes JSON
                         (default: test/sample_data/static_routes.json)
  -h, --help             Show help
```

After startup you'll see:

```
vrouter: loaded N interfaces, M routes
vrouter: listening for events on /tmp/vrouter.events
vrouter:>
```

### CLI commands

| Command                       | Description                              |
| ----------------------------- | ---------------------------------------- |
| `show interfaces`             | List all interfaces with state and stats |
| `show routes`                 | List all routes (connected + static)     |
| `lookup <ip>`                 | Find best route for an IP address        |
| `explain-lookup <ip>`         | Lookup with verbose narration            |
| `shutdown <iface>`            | Set interface admin state to down        |
| `no shutdown <iface>`         | Set interface admin state to up          |
| `help`                        | Show command help                        |
| `quit` / `exit`               | Exit                                     |

### Sending async events

While `vrouter` is running, write to its FIFO from another shell to
simulate link state changes:

```sh
echo "eth0,DOWN" > /tmp/vrouter.events
echo "eth0,UP,eth1,DOWN" > /tmp/vrouter.events    # multiple in one message
```

Format: comma-separated `name,STATE` pairs. State is `UP` or `DOWN`,
case-insensitive. Pairs may also be split across multiple lines.

Events update the **oper** state of an interface. Admin state is
controlled separately by the CLI's `shutdown` / `no shutdown` commands.
An interface is usable for forwarding only when both states are up.

### Config file format

`interfaces.json` — a JSON array:

```json
[
  {
    "name": "eth0",
    "ip_prefix": "10.0.0.1/24",
    "admin_state": "up",
    "oper_state": "up",
    "rx_packets": 1200, "tx_packets": 980,
    "rx_bytes": 240000, "tx_bytes": 180500
  }
]
```

Counter fields are optional and default to 0.

`static_routes.json` — a JSON array:

```json
[
  { "prefix": "172.16.0.0/16", "next_hop_ip": "192.168.1.254", "preference": 10 },
  { "prefix": "172.16.10.0/24", "egress_interface": "eth0",     "preference": 20 }
]
```

Each route must specify `next_hop_ip`, `egress_interface`, or both.
`preference` is optional and defaults to 1. Connected routes
synthesized from interfaces are assigned preference 0.

## Project layout

```
vrouter/
├── Makefile
├── include/                 public headers
├── src/                     implementation
├── test/
│   ├── sample_data/         example config files
│   ├── unit/                Catch2 unit tests
│   └── integration.sh       end-to-end test scenarios
├── third_party/             single-header dependencies (vendored)
├── README.md
└── DESIGN.md
```

