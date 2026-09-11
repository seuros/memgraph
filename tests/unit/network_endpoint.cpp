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

#include "gtest/gtest.h"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "io/network/endpoint.hpp"
#include "io/network/network_error.hpp"

using endpoint_t = memgraph::io::network::Endpoint;

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

TEST(Endpoint, IPv4) {
  endpoint_t endpoint;

  // test constructor
  endpoint = endpoint_t("127.0.0.1", 12'347);
  EXPECT_EQ(endpoint.GetAddress(), "127.0.0.1");
  EXPECT_EQ(endpoint.GetPort(), 12'347);
  EXPECT_EQ(endpoint.GetIpFamily(), endpoint_t::IpFamily::IP4);
}

TEST(Endpoint, IPv6) {
  if (!HasIPv6()) {
    GTEST_SKIP() << "IPv6 not available on this system";
  }
  endpoint_t endpoint;

  // test constructor
  endpoint = endpoint_t("ab:cd:ef::3", 12'347);
  EXPECT_EQ(endpoint.GetAddress(), "ab:cd:ef::3");
  EXPECT_EQ(endpoint.GetPort(), 12'347);
  EXPECT_EQ(endpoint.GetIpFamily(), endpoint_t::IpFamily::IP6);
}

TEST(Endpoint, DNSResolution) {
  endpoint_t endpoint;

  // test constructor
  endpoint = endpoint_t("localhost", 12'347);
  if (endpoint.GetIpFamily() == endpoint_t::IpFamily::IP4) {
    EXPECT_EQ(endpoint.GetResolvedIPAddress(), "127.0.0.1");
    EXPECT_EQ(endpoint.GetPort(), 12'347);
    EXPECT_EQ(endpoint.GetIpFamily(), endpoint_t::IpFamily::IP4);
  } else {
    EXPECT_EQ(endpoint.GetResolvedIPAddress(), "::1");
    EXPECT_EQ(endpoint.GetPort(), 12'347);
    EXPECT_EQ(endpoint.GetIpFamily(), endpoint_t::IpFamily::IP6);
  }
}

TEST(Endpoint, DNSResolutiononParsing) {
  auto const maybe_endpoint = memgraph::io::network::Endpoint::ParseAndCreateSocketOrAddress("localhost:7687");

  ASSERT_EQ(maybe_endpoint.has_value(), true);
  if (maybe_endpoint->GetIpFamily() == endpoint_t::IpFamily::IP4) {
    EXPECT_EQ(maybe_endpoint->GetResolvedIPAddress(), "127.0.0.1");
    EXPECT_EQ(maybe_endpoint->GetPort(), 7687);
  } else {
    EXPECT_EQ(maybe_endpoint->GetResolvedIPAddress(), "::1");
    EXPECT_EQ(maybe_endpoint->GetPort(), 7687);
  }
}
