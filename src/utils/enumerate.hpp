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
/// Polyfill for std::views::enumerate (C++23 P2164R9).
/// libc++ <= 21 does not implement it.

#pragma once

#include <ranges>
#include <version>

#if !defined(__cpp_lib_ranges_enumerate) || __cpp_lib_ranges_enumerate < 202302L

#include <cstddef>
#include <iterator>
#include <tuple>
#include <type_traits>

namespace std::ranges::views {

namespace enumerate_detail {

template <std::ranges::input_range R>
  requires std::ranges::view<R>
class enumerate_view : public std::ranges::view_interface<enumerate_view<R>> {
  R base_;

  class iterator {
    using BaseIter = std::ranges::iterator_t<R>;
    ptrdiff_t index_ = 0;
    BaseIter current_{};

   public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = std::tuple<ptrdiff_t, std::ranges::range_reference_t<R>>;
    using difference_type = ptrdiff_t;

    iterator() = default;
    iterator(BaseIter it, ptrdiff_t idx) : index_(idx), current_(std::move(it)) {}

    value_type operator*() const { return {index_, *current_}; }

    iterator &operator++() {
      ++current_;
      ++index_;
      return *this;
    }
    void operator++(int) { ++*this; }

    friend bool operator==(const iterator &a, const iterator &b) { return a.current_ == b.current_; }
    friend bool operator!=(const iterator &a, const iterator &b) { return !(a == b); }
  };

  class sentinel {
    using BaseSent = std::ranges::sentinel_t<R>;
    BaseSent end_;

   public:
    sentinel() = default;
    explicit sentinel(BaseSent e) : end_(std::move(e)) {}

    friend bool operator==(const iterator &it, const sentinel &s) { return it.base_iter() == s.end_; }
    friend bool operator!=(const iterator &it, const sentinel &s) { return !(it == s); }
    friend bool operator==(const sentinel &s, const iterator &it) { return it == s; }
    friend bool operator!=(const sentinel &s, const iterator &it) { return !(it == s); }
  };

  // Give sentinel access to iterator internals
  friend class sentinel;

 public:
  enumerate_view() = default;
  explicit enumerate_view(R r) : base_(std::move(r)) {}

  auto begin() { return iterator(std::ranges::begin(base_), 0); }

  auto end() {
    if constexpr (std::ranges::common_range<R>) {
      return iterator(std::ranges::end(base_), 0);
    } else {
      return sentinel(std::ranges::end(base_));
    }
  }
};

struct enumerate_fn {
  template <std::ranges::viewable_range R>
  auto operator()(R &&r) const {
    return enumerate_view<std::views::all_t<R>>(std::views::all(std::forward<R>(r)));
  }

  template <std::ranges::viewable_range R>
  friend auto operator|(R &&r, const enumerate_fn &fn) {
    return fn(std::forward<R>(r));
  }
};

}  // namespace enumerate_detail

inline constexpr enumerate_detail::enumerate_fn enumerate{};

}  // namespace std::ranges::views

#endif
