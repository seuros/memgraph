// Copyright 2025 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#include "gtest/gtest.h"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "io/network/utils.hpp"

using namespace memgraph::io::network;

namespace {
bool HasIPv6() {
  struct addrinfo hints{};
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST;
  struct addrinfo *res = nullptr;
  int ret = getaddrinfo("::1", nullptr, &hints, &res);
  if (res) freeaddrinfo(res);
  return ret == 0;
}
}  // namespace

TEST(ResolveHostname, Simple) {
  auto result = ResolveHostname("localhost");
  EXPECT_TRUE(result == "127.0.0.1" || result == "::1");
}

TEST(ResolveHostname, PassThroughIpv4) {
  auto result = ResolveHostname("127.0.0.1");
  EXPECT_EQ(result, "127.0.0.1");
}

TEST(ResolveHostname, PassThroughIpv6) {
  if (!HasIPv6()) {
    GTEST_SKIP() << "IPv6 not available on this system";
  }
  auto result = ResolveHostname("::1");
  EXPECT_EQ(result, "::1");
}
