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

/// @file
/// Polyfill for std::atomic<std::shared_ptr<T>> (C++20 P0718R2).
/// libc++ does not yet implement this specialization.

#pragma once

#include <atomic>
#include <memory>
#include <version>

// Check if the standard library has atomic<shared_ptr> support.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
// Available - nothing to polyfill.
#else

#include <mutex>

namespace std {

template <class T>
class atomic<shared_ptr<T>> {
 public:
  atomic() noexcept = default;
  explicit atomic(shared_ptr<T> p) noexcept : ptr_(std::move(p)) {}

  atomic(const atomic &) = delete;
  atomic &operator=(const atomic &) = delete;

  void store(shared_ptr<T> desired, [[maybe_unused]] memory_order order = memory_order_seq_cst) noexcept {
    std::lock_guard lk(mu_);
    ptr_ = std::move(desired);
  }

  shared_ptr<T> load([[maybe_unused]] memory_order order = memory_order_seq_cst) const noexcept {
    std::lock_guard lk(mu_);
    return ptr_;
  }

  shared_ptr<T> exchange(shared_ptr<T> desired,
                         [[maybe_unused]] memory_order order = memory_order_seq_cst) noexcept {
    std::lock_guard lk(mu_);
    ptr_.swap(desired);
    return desired;
  }

  operator shared_ptr<T>() const noexcept { return load(); }

  void operator=(shared_ptr<T> desired) noexcept { store(std::move(desired)); }

 private:
  mutable std::mutex mu_;
  shared_ptr<T> ptr_;
};

}  // namespace std

#endif
