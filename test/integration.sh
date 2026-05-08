#!/usr/bin/env bash
#
# Integration tests for vrouter. Each scenario:
#   1. Spawns vrouter with sample config + a unique FIFO.
#   2. Pipes a command sequence into stdin, capturing stdout.
#   3. (Optionally) writes events to the FIFO with sleeps to let
#      the worker apply them between commands.
#   4. Asserts expected substrings appear in output.

set -u

VROUTER=./vrouter
INTERFACES=test/sample_data/interfaces.json
ROUTES=test/sample_data/static_routes.json

PASS=0
FAIL=0
FAIL_NAMES=()

# ---- Test infra -------------------------------------------------------------

assert_contains() {
    local name="$1" haystack="$2" needle="$3"
    if grep -qF -- "$needle" <<< "$haystack"; then
        return 0
    else
        echo "  FAIL [$name]: expected substring not found:"
        echo "    needle: $needle"
        echo "  actual output:"
        sed 's/^/    /' <<< "$haystack"
        return 1
    fi
}

assert_not_contains() {
    local name="$1" haystack="$2" needle="$3"
    if grep -qF -- "$needle" <<< "$haystack"; then
        echo "  FAIL [$name]: unexpected substring present: $needle"
        return 1
    fi
    return 0
}

run_scenario() {
    local name="$1"
    echo "[scenario] $name"
    if "$@"; then
        PASS=$((PASS + 1))
        echo "  PASS"
    else
        FAIL=$((FAIL + 1))
        FAIL_NAMES+=("$name")
    fi
}

# Sets a unique $FIFO path. Removes any existing file at that path.
spawn_vrouter() {
    FIFO="/tmp/vrouter.events.$$.${RANDOM}"
    rm -f "$FIFO"
}

# ---- Scenarios --------------------------------------------------------------

scenario_show_interfaces() {
    local name="show_interfaces"
    spawn_vrouter
    local out
    out=$(printf 'show interfaces\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    assert_contains "$name" "$out" "eth0" || return 1
    assert_contains "$name" "$out" "eth1" || return 1
    assert_contains "$name" "$out" "NAME" || return 1
}

scenario_show_routes_includes_connected() {
    local name="show_routes_connected"
    spawn_vrouter
    local out
    out=$(printf 'show routes\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    # Connected routes from interfaces and static routes from JSON.
    assert_contains "$name" "$out" "10.0.0.0/24"     || return 1
    assert_contains "$name" "$out" "(connected)"     || return 1
    assert_contains "$name" "$out" "192.168.1.0/24"  || return 1
    assert_contains "$name" "$out" "172.16.0.0/16"   || return 1
}

scenario_lookup_hits_connected() {
    local name="lookup_connected"
    spawn_vrouter
    local out
    out=$(printf 'lookup 10.0.0.5\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    assert_contains "$name" "$out" "10.0.0.5 -> 10.0.0.0/24"  || return 1
    assert_contains "$name" "$out" "dev eth0"                 || return 1
}

scenario_lookup_falls_through_to_default() {
    local name="lookup_falls_through_to_default"
    spawn_vrouter
    local out
    out=$(printf 'lookup 8.8.8.8\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    # Sample data has a default route via eth1.
    assert_contains "$name" "$out" "8.8.8.8 -> 0.0.0.0/0" || return 1
    assert_contains "$name" "$out" "dev eth1"            || return 1
}

scenario_explain_lookup() {
    local name="explain_lookup"
    spawn_vrouter
    local out
    out=$(printf 'explain-lookup 10.0.0.5\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    assert_contains "$name" "$out" "Looking up 10.0.0.5" || return 1
    assert_contains "$name" "$out" "Result:"             || return 1
}

scenario_shutdown_via_cli() {
    local name="shutdown_cli"
    spawn_vrouter
    local out
    out=$(printf 'lookup 10.0.0.5\nshutdown eth0\nlookup 10.0.0.5\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    # Before shutdown: eth0 connected hit. After: eth0 unusable, the
    # lookup falls through to the default route via eth1.
    assert_contains "$name" "$out" "10.0.0.5 -> 10.0.0.0/24 via (connected) dev eth0" || return 1
    assert_contains "$name" "$out" "10.0.0.5 -> 0.0.0.0/0 via 192.168.1.254 dev eth1" || return 1
}

scenario_oper_state_via_fifo() {
    local name="oper_state_fifo"
    spawn_vrouter

    # Spawn vrouter as a background coprocess so we can send events
    # concurrently with CLI commands.
    coproc VR { "$VROUTER" --fifo "$FIFO" \
                  --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null; }
    local pid=$VR_PID

    # Wait for vrouter to set up the FIFO.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        [[ -p "$FIFO" ]] && break
        sleep 0.1
    done

    # Bring eth0 oper down via FIFO, then look up something that
    # would normally hit eth0. Should fall through to the default.
    echo "eth0,DOWN" > "$FIFO"
    sleep 0.2  # let the worker apply

    printf 'lookup 10.0.0.5\nquit\n' >&"${VR[1]}"
    local out
    out=$(cat <&"${VR[0]}")
    wait "$pid" 2>/dev/null
    rm -f "$FIFO"

    assert_contains "$name" "$out" "10.0.0.5 -> 0.0.0.0/0 via 192.168.1.254 dev eth1" || return 1
}

scenario_malformed_event_tolerated() {
    local name="malformed_event"
    spawn_vrouter

    coproc VR { "$VROUTER" --fifo "$FIFO" \
                  --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null; }
    local pid=$VR_PID

    for _ in 1 2 3 4 5 6 7 8 9 10; do
        [[ -p "$FIFO" ]] && break
        sleep 0.1
    done

    # Garbage, then a valid event. CLI should still be alive.
    echo "this,is,not,valid" > "$FIFO"
    echo "eth0,DOWN"          > "$FIFO"
    sleep 0.2

    printf 'lookup 10.0.0.5\nquit\n' >&"${VR[1]}"
    local out
    out=$(cat <&"${VR[0]}")
    wait "$pid" 2>/dev/null
    rm -f "$FIFO"

    # Process didn't crash; lookup ran.
    assert_contains "$name" "$out" "10.0.0.5 ->" || return 1
}

scenario_lpm_picks_longer_prefix() {
    local name="lpm_longer_prefix"
    spawn_vrouter
    local out
    out=$(printf 'lookup 10.10.5.7\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    # 10.10.5.7 matches both 10.10.0.0/16 and 10.10.5.0/24; /24 wins.
    assert_contains "$name" "$out" "10.10.5.7 -> 10.10.5.0/24" || return 1
}

scenario_preference_breaks_tie() {
    local name="preference_tie_break"
    spawn_vrouter
    local out
    out=$(printf 'lookup 172.16.10.5\nquit\n' | \
          "$VROUTER" --fifo "$FIFO" \
              --interfaces "$INTERFACES" --routes "$ROUTES" 2>/dev/null)
    rm -f "$FIFO"

    # Two /24 routes for 172.16.10.0/24, prefs 10 and 50; pref 10 wins.
    assert_contains "$name" "$out" "172.16.10.5 -> 172.16.10.0/24 via 172.16.0.254" || return 1
}

# ---- Run --------------------------------------------------------------------

if [[ ! -x "$VROUTER" ]]; then
    echo "vrouter binary not found at $VROUTER; run 'make' first."
    exit 1
fi

run_scenario scenario_show_interfaces
run_scenario scenario_show_routes_includes_connected
run_scenario scenario_lookup_hits_connected
run_scenario scenario_lookup_falls_through_to_default
run_scenario scenario_lpm_picks_longer_prefix
run_scenario scenario_preference_breaks_tie
run_scenario scenario_explain_lookup
run_scenario scenario_shutdown_via_cli
run_scenario scenario_oper_state_via_fifo
run_scenario scenario_malformed_event_tolerated

echo
echo "===================="
echo "Passed: $PASS"
echo "Failed: $FAIL"
if (( FAIL > 0 )); then
    printf 'Failed scenarios:\n'
    printf '  - %s\n' "${FAIL_NAMES[@]}"
    exit 1
fi
exit 0
