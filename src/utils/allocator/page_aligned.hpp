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

#include <algorithm>
#include <cstddef>
#include <new>

#include "utils/db_aware_allocator.hpp"

namespace memgraph::utils {

// custom allocator, ensures all allocations are page aligned.
// Allocations follow the DB arena currently pinned in TLS, so callers must
// establish DbArenaScope / DbAwareThread before using containers that rely on
// this allocator.
template <typename T>
struct PageAlignedAllocator {
  static constexpr std::size_t kPageSize = 4096;
  using value_type = T;

  PageAlignedAllocator() = default;

  template <class U>
  explicit PageAlignedAllocator(const PageAlignedAllocator<U> & /*other*/) noexcept {}

  auto allocate(std::size_t n) -> T * {
    auto size = std::max(n * sizeof(T), kPageSize);
    // Round up to the nearest multiple of kPageSize
    size = ((size + kPageSize - 1) / kPageSize) * kPageSize;
    return static_cast<T *>(memory::DbAllocateBytes(size, memory::tls_db_arena_state.arena, kPageSize));
  }

  void deallocate(T *p, std::size_t n) const noexcept {
    // NOTE: jemalloc tracks the owning arena per-extent in its own metadata, so GC can safely
    // free query-thread allocations regardless of which thread calls deallocate.
    // Recalculate the actual allocated size (mirroring allocate) for sized deallocation.
    auto size = std::max(n * sizeof(T), kPageSize);
    size = ((size + kPageSize - 1) / kPageSize) * kPageSize;
    memory::DbDeallocateBytes(static_cast<void *>(p), size, kPageSize);
  }

  friend bool operator==(PageAlignedAllocator const &, PageAlignedAllocator const &) noexcept { return true; }

  friend bool operator!=(PageAlignedAllocator const &lhs, PageAlignedAllocator const &rhs) noexcept {
    return !(lhs == rhs);
  }
};

}  // namespace memgraph::utils
