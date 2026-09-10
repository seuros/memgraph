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
/// Polyfill for std::move_only_function (C++23 P0288R9).
/// libc++ ≤21 does not implement it.  On implementations that have it,
/// this header is a transparent alias.

#pragma once

#include <version>

#if __cpp_lib_move_only_function >= 202110L

#include <functional>
// std::move_only_function is available - nothing to polyfill.

#else

#include <memory>
#include <type_traits>
#include <utility>

namespace std {

template <class>
class move_only_function;  // primary - intentionally undefined

template <class R, class... Args>
class move_only_function<R(Args...)> {
  struct concept_t {
    virtual R invoke(Args...) = 0;
    virtual ~concept_t() = default;
  };

  template <class F>
  struct model_t final : concept_t {
    F f_;
    template <class G>
    explicit model_t(G &&g) : f_(std::forward<G>(g)) {}
    R invoke(Args... args) override {
      if constexpr (std::is_void_v<R>) {
        f_(std::forward<Args>(args)...);
      } else {
        return f_(std::forward<Args>(args)...);
      }
    }
  };

  std::unique_ptr<concept_t> impl_;

 public:
  move_only_function() noexcept = default;
  move_only_function(std::nullptr_t) noexcept {}

  template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, move_only_function>>>
  move_only_function(F &&f) : impl_(std::make_unique<model_t<std::decay_t<F>>>(std::forward<F>(f))) {}

  move_only_function(move_only_function &&) noexcept = default;
  move_only_function &operator=(move_only_function &&) noexcept = default;
  move_only_function &operator=(std::nullptr_t) noexcept {
    impl_.reset();
    return *this;
  }

  explicit operator bool() const noexcept { return static_cast<bool>(impl_); }

  R operator()(Args... args) {
    if constexpr (std::is_void_v<R>) {
      impl_->invoke(std::forward<Args>(args)...);
    } else {
      return impl_->invoke(std::forward<Args>(args)...);
    }
  }
};

}  // namespace std

#endif
