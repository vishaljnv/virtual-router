#include "events.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "router.h"

namespace vrouter {

namespace {

constexpr std::size_t kReadBufSize = 4096;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) { out.push_back(std::move(cur)); cur.clear(); }
        else            { cur.push_back(c); }
    }
    out.push_back(std::move(cur));
    return out;
}

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}

void drain(int fd) {
    char buf[kReadBufSize];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) continue;
        if (n == 0) break;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        throw_errno("drain: read");
    }
}

void apply_message(Router& router, const std::string& msg) {
    if (msg.empty()) return;

    auto tokens = split(msg, ',');
    if (tokens.size() % 2 != 0) {
        std::cerr << "[event] malformed message (odd token count): "
                  << msg << "\n";
        return;
    }

    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
        const std::string& name = tokens[i];
        const std::string  st   = lower(tokens[i + 1]);

        if (name.empty()) {
            std::cerr << "[event] empty interface name in message\n";
            continue;
        }

        bool up;
        if      (st == "up")   up = true;
        else if (st == "down") up = false;
        else {
            std::cerr << "[event] invalid state '" << tokens[i + 1]
                      << "' for " << name << "\n";
            continue;
        }

        if (!router.set_oper_state(name, up)) {
            std::cerr << "[event] no such interface: " << name << "\n";
            continue;
        }

        std::cerr << "[event] " << name << " oper -> "
                  << (up ? "up" : "down") << "\n";
    }
}

}  // namespace

EventListener::EventListener(Router& router, std::string fifo_path)
    : router_{router}, fifo_path_{std::move(fifo_path)} {}

EventListener::~EventListener() {
    stop();
}

void EventListener::start() {
    if (running_.load()) return;

    if (::unlink(fifo_path_.c_str()) == -1 && errno != ENOENT) {
        throw_errno("unlink " + fifo_path_);
    }
    if (::mkfifo(fifo_path_.c_str(), 0666) == -1) {
        throw_errno("mkfifo " + fifo_path_);
    }

    fifo_fd_ = ::open(fifo_path_.c_str(), O_RDWR | O_NONBLOCK);
    if (fifo_fd_ == -1) throw_errno("open fifo");

    drain(fifo_fd_);

    shutdown_fd_ = ::eventfd(0, EFD_NONBLOCK);
    if (shutdown_fd_ == -1) throw_errno("eventfd");

    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ == -1) throw_errno("epoll_create1");

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = fifo_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fifo_fd_, &ev) == -1) {
        throw_errno("epoll_ctl(fifo)");
    }
    ev.data.fd = shutdown_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, shutdown_fd_, &ev) == -1) {
        throw_errno("epoll_ctl(shutdown)");
    }

    running_.store(true);
    thread_ = std::thread{&EventListener::run, this};
}

void EventListener::stop() {
    if (running_.exchange(false)) {
        // Was running: wake and join the thread.
        if (shutdown_fd_ != -1) {
            uint64_t one = 1;
            ssize_t  n   = ::write(shutdown_fd_, &one, sizeof(one));
            (void)n;
        }
        if (thread_.joinable()) thread_.join();
    }

    // Always: release any resources we hold, regardless of whether
    // start() completed successfully.
    if (epoll_fd_    != -1) { ::close(epoll_fd_);    epoll_fd_    = -1; }
    if (shutdown_fd_ != -1) { ::close(shutdown_fd_); shutdown_fd_ = -1; }
    if (fifo_fd_     != -1) { ::close(fifo_fd_);     fifo_fd_     = -1; }
    if (!fifo_path_.empty()) ::unlink(fifo_path_.c_str());
}

void EventListener::run() {
    std::string buf;
    epoll_event events[2];

    while (running_.load()) {
        int n = ::epoll_wait(epoll_fd_, events, 2, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            std::cerr << "[event] epoll_wait: " << std::strerror(errno) << "\n";
            return;
        }

        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == shutdown_fd_) {
                return;
            }
            if (events[i].data.fd == fifo_fd_) {
                char chunk[kReadBufSize];
                while (true) {
                    ssize_t r = ::read(fifo_fd_, chunk, sizeof(chunk));
                    if (r > 0) {
                        buf.append(chunk, static_cast<std::size_t>(r));
                        continue;
                    }
                    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                    if (r == -1 && errno == EINTR) continue;
                    if (r == 0) break;
                    std::cerr << "[event] read: " << std::strerror(errno) << "\n";
                    return;
                }

                std::size_t pos;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, pos);
                    buf.erase(0, pos + 1);
                    apply_message(router_, line);
                }
            }
        }
    }
}

}  // namespace vrouter
