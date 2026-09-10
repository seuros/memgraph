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

#include "utils/sysinfo/memory.hpp"

#include <spdlog/spdlog.h>

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <vm/vm_param.h>
#else
#include <sys/sysinfo.h>
#include <fmt/format.h>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#endif

namespace memgraph::utils::sysinfo {

#ifdef __FreeBSD__

namespace {
std::optional<uint64_t> SysctlByName(const char *name) {
  uint64_t value = 0;
  size_t len = sizeof(value);
  if (sysctlbyname(name, &value, &len, nullptr, 0) != 0) {
    SPDLOG_WARN("Failed to read sysctl {}", name);
    return std::nullopt;
  }
  return value;
}
}  // namespace

std::optional<uint64_t> AvailableMemory() {
  // Free + inactive + cache pages, converted to KiB
  auto page_size = SysctlByName("hw.pagesize");
  auto free_count = SysctlByName("vm.stats.vm.v_free_count");
  auto inactive_count = SysctlByName("vm.stats.vm.v_inactive_count");
  auto cache_count = SysctlByName("vm.stats.vm.v_cache_count");
  if (!page_size || !free_count || !inactive_count) return std::nullopt;
  uint64_t pages = *free_count + *inactive_count + cache_count.value_or(0);
  return (pages * *page_size) / 1024;
}

std::optional<MemoryCapacity> InstalledMemory() {
  auto ram = SysctlByName("hw.physmem");
  auto swap = SysctlByName("vm.swap_total");
  if (!ram) return std::nullopt;
  return MemoryCapacity{.ram_kib = *ram / 1024, .swap_kib = swap.value_or(0) / 1024};
}

#else  // Linux

namespace {
std::optional<uint64_t> ExtractAmountFromMemInfo(const std::string_view header_name) {
  std::string token;
  std::ifstream meminfo("/proc/meminfo");
  const auto meminfo_header = fmt::format("{}:", header_name);
  while (meminfo >> token) {
    if (token == meminfo_header) {
      uint64_t mem = 0;
      if (meminfo >> mem) {
        return mem;
      } else {
        return std::nullopt;
      }
    }
    meminfo.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  SPDLOG_WARN("Failed to read {} from /proc/meminfo", header_name);
  return std::nullopt;
}

}  // namespace

// MemAvailable is a kernel estimate with no syscall equivalent, so it still comes from /proc/meminfo.
std::optional<uint64_t> AvailableMemory() { return ExtractAmountFromMemInfo("MemAvailable"); }

std::optional<MemoryCapacity> InstalledMemory() {
  struct ::sysinfo info{};
  if (::sysinfo(&info) != 0) {
    SPDLOG_WARN("sysinfo() failed");
    return std::nullopt;
  }
  const uint64_t mem_unit = info.mem_unit;
  return MemoryCapacity{.ram_kib = info.totalram * mem_unit / 1024, .swap_kib = info.totalswap * mem_unit / 1024};
}

#endif

}  // namespace memgraph::utils::sysinfo
