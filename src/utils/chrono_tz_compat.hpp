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
/// Polyfill for C++20 <chrono> timezone support on platforms where libc++
/// lacks it (e.g., FreeBSD).  On implementations that already provide the
/// full std::chrono timezone API this header is empty.
///
/// The shim uses POSIX mktime/localtime_r and the system zoneinfo database
/// (/usr/share/zoneinfo).  It is NOT a full implementation of the C++20
/// timezone library - only the subset that memgraph uses is covered.

#pragma once

#include <chrono>
#include <version>

// Check if the standard library provides timezone support.
// libstdc++ ≥ 13 and libc++ (future) define __cpp_lib_chrono ≥ 201907L
// or provide std::chrono::time_zone directly.
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
// Full timezone support available - nothing to do.
#else

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include <sys/stat.h>
#include <unistd.h>

namespace std::chrono {

// Forward declarations
class time_zone;
const time_zone *locate_zone(std::string_view name);
const time_zone *current_zone();

enum class choose { earliest, latest };

struct sys_info {
  sys_seconds begin;
  sys_seconds end;
  seconds offset;
  minutes save;
  std::string abbrev;
};

/// Minimal time_zone implementation backed by POSIX APIs.
///
/// Strategy: temporarily set TZ to the zone name, call mktime/localtime_r,
/// then restore TZ.  This is protected by a global mutex.
class time_zone {
 public:
  explicit time_zone(std::string tz_name) : name_(std::move(tz_name)) {}

  [[nodiscard]] std::string_view name() const noexcept { return name_; }

  template <class Duration>
  sys_info get_info(const sys_time<Duration> &st) const {
    auto secs = std::chrono::time_point_cast<seconds>(st);
    auto tt = static_cast<std::time_t>(secs.time_since_epoch().count());

    std::tm tm_buf{};
    {
      TzGuard guard(name_);
      localtime_r(&tt, &tm_buf);
    }

    sys_info info;
    info.offset = seconds{tm_buf.tm_gmtoff};
    info.save = minutes{tm_buf.tm_isdst > 0 ? 60 : 0};
    info.abbrev = tm_buf.tm_zone ? tm_buf.tm_zone : "";
    // Approximate begin/end - POSIX doesn't give exact transition points.
    // Use very wide range for the current rule.
    info.begin = sys_seconds{seconds{0}};
    info.end = sys_seconds{seconds{std::numeric_limits<int32_t>::max()}};
    return info;
  }

  template <class Duration>
  auto to_local(const sys_time<Duration> &st) const -> local_time<Duration> {
    auto secs = std::chrono::time_point_cast<seconds>(st);
    auto tt = static_cast<std::time_t>(secs.time_since_epoch().count());

    std::tm tm_buf{};
    {
      TzGuard guard(name_);
      localtime_r(&tt, &tm_buf);
    }

    auto offset_s = seconds{tm_buf.tm_gmtoff};
    return local_time<Duration>{st.time_since_epoch() + duration_cast<Duration>(offset_s)};
  }

  template <class Duration>
  auto to_sys(const local_time<Duration> &lt, [[maybe_unused]] choose c = choose::earliest) const
      -> sys_time<common_type_t<Duration, seconds>> {
    using ResultDuration = common_type_t<Duration, seconds>;

    // Convert local_time to struct tm, then use mktime with the target TZ.
    auto local_secs = duration_cast<seconds>(lt.time_since_epoch());
    // Treat local_time epoch as if it were UTC for the purpose of
    // decomposing into y/m/d h:m:s, then let mktime interpret it as local.
    auto tt_utc = static_cast<std::time_t>(local_secs.count());
    std::tm tm_buf{};
    gmtime_r(&tt_utc, &tm_buf);  // decompose without offset
    tm_buf.tm_isdst = -1;        // let mktime figure out DST

    std::time_t result;
    {
      TzGuard guard(name_);
      result = mktime(&tm_buf);
    }

    if (result == static_cast<std::time_t>(-1)) {
      // mktime failed - fall back to treating local as UTC
      return sys_time<ResultDuration>{duration_cast<ResultDuration>(local_secs)};
    }

    auto sys_secs = seconds{result};
    // Preserve sub-second precision from the original local_time.
    auto subsec = lt.time_since_epoch() - duration_cast<Duration>(local_secs);
    return sys_time<ResultDuration>{duration_cast<ResultDuration>(sys_secs) + duration_cast<ResultDuration>(subsec)};
  }

 private:
  std::string name_;

  /// RAII helper: sets TZ, calls tzset(), restores on destruction.
  struct TzGuard {
    static std::mutex &mu() {
      static std::mutex m;
      return m;
    }

    std::unique_lock<std::mutex> lock_;
    std::string old_tz_;
    bool had_tz_;

    explicit TzGuard(const std::string &tz_name) : lock_(mu()) {
      const char *prev = std::getenv("TZ");
      had_tz_ = (prev != nullptr);
      if (had_tz_) old_tz_ = prev;

      std::string tz_val = ":" + tz_name;
      setenv("TZ", tz_val.c_str(), 1);
      tzset();
    }

    ~TzGuard() {
      if (had_tz_) {
        setenv("TZ", old_tz_.c_str(), 1);
      } else {
        unsetenv("TZ");
      }
      tzset();
    }

    TzGuard(const TzGuard &) = delete;
    TzGuard &operator=(const TzGuard &) = delete;
  };
};

/// Registry of time_zone objects keyed by the caller-supplied name.
/// Each distinct name (including IANA links like "US/Pacific") gets its own
/// time_zone whose name() returns exactly what was requested - matching
/// libstdc++ behaviour.
inline const time_zone *locate_zone(std::string_view name) {
  static std::mutex mu;
  static std::unordered_map<std::string, std::unique_ptr<time_zone>> zones;

  auto requested = std::string(name);

  // Validate that the zone exists in the system zoneinfo database.
  std::string tz_path = "/usr/share/zoneinfo/" + requested;
  struct stat st;
  if (::stat(tz_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    throw std::runtime_error("unknown timezone: " + requested);
  }

  std::lock_guard lk(mu);
  auto it = zones.find(requested);
  if (it != zones.end()) return it->second.get();
  auto [ins, _] = zones.emplace(requested, std::make_unique<time_zone>(requested));
  return ins->second.get();
}

/// Returns the system's current timezone.
inline const time_zone *current_zone() {
  // Try $TZ first.
  if (const char *tz = std::getenv("TZ"); tz && *tz) {
    std::string_view sv{tz};
    if (sv.front() == ':') sv.remove_prefix(1);
    return locate_zone(sv);
  }

  // Read /etc/localtime symlink.
  char buf[256];
  auto len = ::readlink("/etc/localtime", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    std::string_view target{buf, static_cast<size_t>(len)};
    auto pos = target.find("/zoneinfo/");
    if (pos != std::string_view::npos) {
      return locate_zone(target.substr(pos + 10));
    }
  }

  return locate_zone("UTC");
}

// zoned_traits - default implementation and the specialization point
// that memgraph uses.
template <class TimeZonePtr>
struct zoned_traits {};

template <>
struct zoned_traits<const time_zone *> {
  static const time_zone *default_zone() { return locate_zone("UTC"); }
};

/// Minimal zoned_time implementation.
template <class Duration, class TimeZonePtr = const time_zone *>
class zoned_time {
 public:
  zoned_time() : zone_(zoned_traits<TimeZonePtr>::default_zone()), tp_{} {}

  zoned_time(TimeZonePtr z, const sys_time<Duration> &st) : zone_(std::move(z)), tp_(st) {}

  zoned_time(TimeZonePtr z, const local_time<Duration> &lt, choose c = choose::earliest)
      : zone_(std::move(z)), tp_(zone_->to_sys(lt, c)) {}

  zoned_time(std::string_view name, const sys_time<Duration> &st) : zone_(locate_zone(name)), tp_(st) {}

  sys_time<Duration> get_sys_time() const { return tp_; }

  local_time<Duration> get_local_time() const { return zone_->to_local(tp_); }

  const TimeZonePtr &get_time_zone() const { return zone_; }

 private:
  TimeZonePtr zone_;
  sys_time<Duration> tp_;
};

}  // namespace std::chrono

// The std::formatter specialization for zoned_time is in a separate header
// (chrono_tz_fmt.hpp) to avoid triggering a clang C++20 modules crash
// (setFunctionTemplateSpecialization assert) when this header is included
// in a global module fragment.

#endif  // timezone support check
