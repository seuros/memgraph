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
/// std::formatter specialization for the polyfill std::chrono::zoned_time.
/// Separated from chrono_tz_compat.hpp to avoid a clang C++20 modules crash
/// when the formatter template specialization appears in a global module fragment.

#pragma once

#include "utils/chrono_tz_compat.hpp"

#if !defined(__cpp_lib_chrono) || __cpp_lib_chrono < 201907L

#include <format>

template <class Duration, class TimeZonePtr>
struct std::formatter<std::chrono::zoned_time<Duration, TimeZonePtr>, char> {
 private:
  // Store the format spec as a fixed-size array to keep parse() constexpr-friendly.
  char spec_[64]{};
  size_t spec_len_{0};

 public:
  constexpr auto parse(std::format_parse_context &ctx) {
    auto it = ctx.begin();
    auto end = ctx.end();
    spec_len_ = 0;
    while (it != end && *it != '}' && spec_len_ < sizeof(spec_) - 1) {
      spec_[spec_len_++] = *it;
      ++it;
    }
    spec_[spec_len_] = '\0';
    return it;
  }

  auto format(const std::chrono::zoned_time<Duration, TimeZonePtr> &zt, std::format_context &ctx) const
      -> std::format_context::iterator {
    auto local_tp = zt.get_local_time();
    auto sys_tp = zt.get_sys_time();
    const auto &zone = zt.get_time_zone();
    auto info = zone->get_info(sys_tp);

    // Decompose local time
    auto local_secs = std::chrono::duration_cast<std::chrono::seconds>(local_tp.time_since_epoch());
    auto tt = static_cast<std::time_t>(local_secs.count());
    std::tm tm{};
    gmtime_r(&tt, &tm);
    tm.tm_gmtoff = info.offset.count();

    // Build strftime format, resolving %Ez inline
    char strftime_spec[128]{};
    size_t si = 0;
    bool has_S = false;
    for (size_t i = 0; i < spec_len_ && si < sizeof(strftime_spec) - 8; ++i) {
      if (spec_[i] == '%' && i + 1 < spec_len_) {
        if (spec_[i + 1] == 'E' && i + 2 < spec_len_ && spec_[i + 2] == 'z') {
          // %Ez → compute offset string with colon
          auto off = info.offset.count();
          char sign = off >= 0 ? '+' : '-';
          if (off < 0) off = -off;
          int h = static_cast<int>(off / 3600);
          int m = static_cast<int>((off % 3600) / 60);
          si += static_cast<size_t>(snprintf(strftime_spec + si, sizeof(strftime_spec) - si, "%c%02d:%02d", sign, h, m));
          i += 2;
          continue;
        }
        if (spec_[i + 1] == 'S') has_S = true;
        strftime_spec[si++] = '%';
        strftime_spec[si++] = spec_[++i];
      } else {
        strftime_spec[si++] = spec_[i];
      }
    }
    strftime_spec[si] = '\0';

    char buf[256];
    auto len = strftime(buf, sizeof(buf), strftime_spec, &tm);
    std::string result(buf, len);

    // Append sub-second precision after seconds if %S was used
    if (has_S) {
      auto subsec = local_tp.time_since_epoch() - std::chrono::duration_cast<Duration>(local_secs);
      auto us = std::chrono::duration_cast<std::chrono::microseconds>(subsec).count();
      if (us != 0) {
        char secstr[3];
        snprintf(secstr, sizeof(secstr), "%02d", tm.tm_sec);
        auto pos = result.rfind(secstr);
        if (pos != std::string::npos) {
          char usbuf[16];
          snprintf(usbuf, sizeof(usbuf), ".%06lld", static_cast<long long>(us < 0 ? -us : us));
          std::string us_str(usbuf);
          while (us_str.size() > 2 && us_str.back() == '0') us_str.pop_back();
          result.insert(pos + 2, us_str);
        }
      }
    }

    return std::format_to(ctx.out(), "{}", result);
  }
};

#endif  // timezone support check
