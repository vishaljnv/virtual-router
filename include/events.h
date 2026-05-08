#ifndef VROUTER_EVENTS_H
#define VROUTER_EVENTS_H

#include <atomic>
#include <string>
#include <thread>

namespace vrouter {

class Router;

// Watches a named FIFO for interface state-change events and applies
// them to the Router.
//
// Wire format (newlines separate logical messages, commas separate
// tokens within a message; case-insensitive UP/DOWN):
//
//   eth0,UP
//   eth0,UP,eth1,DOWN
//
// Each (name, state) pair updates the OPER state of the named
// interface (admin state is operator-controlled via the CLI).
//
// Lifecycle:
//   construct -> start() -> ... -> stop() -> destruct
//   stop() is idempotent and is also called from the destructor as
//   a safety net.
class EventListener {
public:
    EventListener(Router& router, std::string fifo_path);
    ~EventListener();

    EventListener(const EventListener&)            = delete;
    EventListener& operator=(const EventListener&) = delete;

    // Creates the FIFO (replacing any existing file at the path),
    // opens it, drains any buffered data, and spawns the worker
    // thread. Throws std::runtime_error on any setup failure.
    void start();

    // Signals the worker thread to exit, joins it, closes fds,
    // and unlinks the FIFO. Safe to call multiple times.
    void stop();

private:
    void run();  // worker thread body

    Router&           router_;
    std::string       fifo_path_;

    int               fifo_fd_     = -1;
    int               shutdown_fd_ = -1;   // eventfd
    int               epoll_fd_    = -1;
    std::thread       thread_;
    std::atomic<bool> running_{false};
};

}  // namespace vrouter

#endif  // VROUTER_EVENTS_H
