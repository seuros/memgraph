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
/// Header-based declarations for S3Config and related types, extracted from
/// aws.cppm for use on platforms where clang C++20 module emission crashes.
/// On non-FreeBSD platforms, these types are provided via `import memgraph.utils.aws;`.

#pragma once

#include <cstdlib>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace memgraph::utils {

using namespace std::string_view_literals;

constexpr auto kAwsRegionQuerySetting = "aws_region"sv;
constexpr auto kAwsAccessKeyQuerySetting = "aws_access_key"sv;
constexpr auto kAwsSecretKeyQuerySetting = "aws_secret_key"sv;
constexpr auto kAwsEndpointUrlQuerySetting = "aws_endpoint_url"sv;

constexpr auto kAwsAccessKeyEnv = "AWS_ACCESS_KEY";
constexpr auto kAwsRegionEnv = "AWS_REGION";
constexpr auto kAwsSecretKeyEnv = "AWS_SECRET_KEY";
constexpr auto kAwsEndpointUrlEnv = "AWS_ENDPOINT_URL";

enum class AwsValidationError : uint8_t { AWS_REGION, AWS_ACCESS_KEY, AWS_SECRET_KEY };

inline auto AwsValidationErrorToStr(AwsValidationError err) -> std::string {
  switch (err) {
    using enum AwsValidationError;
    case AWS_REGION:
      return fmt::format(
          "AWS region configuration parameter not provided. Please provide it through the query, run-time setting {} "
          "or env variable {}",
          kAwsRegionQuerySetting, kAwsRegionEnv);
    case AWS_ACCESS_KEY:
      return fmt::format(
          "AWS access key configuration parameter not provided. Please provide it through the query, run-time "
          "setting {} or env variable {}",
          kAwsAccessKeyQuerySetting, kAwsAccessKeyEnv);
    case AWS_SECRET_KEY:
      return fmt::format(
          "AWS secret key configuration parameter not provided. Please provide it through the query, run-time "
          "setting {} or env variable {}",
          kAwsSecretKeyQuerySetting, kAwsSecretKeyEnv);
  }
}

struct S3Config {
  std::optional<std::string> aws_region;
  std::optional<std::string> aws_access_key;
  std::optional<std::string> aws_secret_key;
  std::optional<std::string> aws_endpoint_url;

  [[nodiscard]] auto Validate() const -> std::optional<AwsValidationError> {
    if (!aws_region.has_value()) return AwsValidationError::AWS_REGION;
    if (!aws_access_key.has_value()) return AwsValidationError::AWS_ACCESS_KEY;
    if (!aws_secret_key.has_value()) return AwsValidationError::AWS_SECRET_KEY;
    return std::nullopt;
  }

  static auto Build(std::map<std::string, std::string, std::less<>> query_config,
                    std::map<std::string, std::string, std::less<>> run_time_config) -> S3Config;
};

struct S3Error {
  std::string message;
};

auto GetS3Object(std::string uri, S3Config const &s3_config, std::string const &target_path)
    -> std::expected<void, S3Error>;
auto GetS3Object(std::string uri, S3Config const &s3_config, std::ostream &ostream) -> std::expected<void, S3Error>;

}  // namespace memgraph::utils
