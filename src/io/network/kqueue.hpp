// Copyright 2026 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#pragma once

#include <errno.h>
#include <fmt/format.h>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include "io/network/socket.hpp"
#include "utils/exceptions.hpp"
#include "utils/likely.hpp"
#include "utils/logging.hpp"

// Epoll-compatible constants for source compatibility with Linux code.
#ifndef EPOLLIN
#define EPOLLIN    0x001u
#define EPOLLOUT   0x004u
#define EPOLLERR   0x008u
#define EPOLLHUP   0x010u
#define EPOLLRDHUP 0x2000u
#define EPOLLET    (1u << 31)
#define EPOLLONESHOT (1u << 30)
#endif

namespace memgraph::io::network {

/**
 * Wrapper class for kqueue (FreeBSD equivalent of Linux epoll).
 * Provides the same interface as the Epoll class.
 */
class Epoll {
 public:
  // Use a struct matching epoll_event layout for source compatibility.
  struct Event {
    uint32_t events{0};
    union {
      void *ptr{nullptr};
    } data;
  };

  Epoll() : kq_fd_(kqueue()) {
    if (kq_fd_ == -1) {
      throw utils::BasicException("Error on kqueue create: ({}) {}", errno, strerror(errno));
    }
  }

  Epoll(const Epoll &) = delete;
  Epoll &operator=(const Epoll &) = delete;
  Epoll(Epoll &&) = delete;
  Epoll &operator=(Epoll &&) = delete;

  ~Epoll() { close(kq_fd_); }

  void Add(int fd, uint32_t events, void *ptr, bool modify = false) {
    // kqueue doesn't distinguish add/modify - EV_ADD is idempotent.
    (void)modify;

    struct kevent changes[2];
    int nchanges = 0;

    uint16_t flags = EV_ADD;
    if (events & EPOLLET) flags |= EV_CLEAR;       // edge-triggered
    if (events & EPOLLONESHOT) flags |= EV_ONESHOT;

    if (events & EPOLLIN) {
      EV_SET(&changes[nchanges++], fd, EVFILT_READ, flags, 0, 0, ptr);
    }
    if (events & EPOLLOUT) {
      EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, flags, 0, 0, ptr);
    }

    if (nchanges > 0) {
      int status = kevent(kq_fd_, changes, nchanges, nullptr, 0, nullptr);
      MG_ASSERT(status != -1, "Error on kqueue {}: ({}) {}", (modify ? "modify" : "add"), errno, strerror(errno));
    }
  }

  void Modify(int fd, uint32_t events, void *ptr) { Add(fd, events, ptr, true); }

  void Delete(int fd) {
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    // Ignore errors - the filter may not have been registered.
    kevent(kq_fd_, changes, 2, nullptr, 0, nullptr);
  }

  int Wait(Event *events, int max_events, int timeout_ms) {
    // Use a reasonable fixed upper bound; the caller typically passes 1-64.
    static constexpr int kMaxKevents = 64;
    MG_ASSERT(max_events <= kMaxKevents, "kqueue Wait: max_events {} exceeds limit {}", max_events, kMaxKevents);
    struct kevent kevents[kMaxKevents];

    struct timespec ts;
    struct timespec *ts_ptr = nullptr;
    if (timeout_ms >= 0) {
      ts.tv_sec = timeout_ms / 1000;
      ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
      ts_ptr = &ts;
    }

    int n = kevent(kq_fd_, nullptr, 0, kevents, max_events, ts_ptr);
    if (n == -1) {
      if (errno == EINTR) return 0;
      MG_ASSERT(false, "Error on kqueue wait: ({}) {}", errno, strerror(errno));
    }

    // Translate kevent results to our Event format.
    // Multiple kevents for the same udata (fd) are merged into one Event.
    int count = 0;
    for (int i = 0; i < n && count < max_events; ++i) {
      auto &kev = kevents[i];
      // Check if we already have an event for this udata
      bool merged = false;
      for (int j = 0; j < count; ++j) {
        if (events[j].data.ptr == kev.udata) {
          if (kev.filter == EVFILT_READ) events[j].events |= EPOLLIN;
          if (kev.filter == EVFILT_WRITE) events[j].events |= EPOLLOUT;
          if (kev.flags & EV_EOF) events[j].events |= EPOLLHUP;
          if (kev.flags & EV_ERROR) events[j].events |= EPOLLERR;
          merged = true;
          break;
        }
      }
      if (!merged) {
        events[count].events = 0;
        events[count].data.ptr = kev.udata;
        if (kev.filter == EVFILT_READ) events[count].events |= EPOLLIN;
        if (kev.filter == EVFILT_WRITE) events[count].events |= EPOLLOUT;
        if (kev.flags & EV_EOF) events[count].events |= EPOLLHUP;
        if (kev.flags & EV_ERROR) events[count].events |= EPOLLERR;
        ++count;
      }
    }
    return count;
  }

 private:
  const int kq_fd_;
};

}  // namespace memgraph::io::network
