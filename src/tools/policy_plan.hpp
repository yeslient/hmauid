// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <expected>

#include "netlink.hpp"
#include "rules.hpp"

namespace tosya {

struct PolicyPlan {
  Pairs pairs;
  std::vector<HiddenName> names;
  std::map<std::string, std::uint32_t, std::less<>> callers;
};

/* Compute desired state from an already loaded snapshot. An error carries the
 * expanded pair count; the caller must keep its previously published state. */
[[nodiscard]] std::expected<PolicyPlan, std::size_t>
build_policy_plan(const Rules &rules, const Presets &presets,
                  const PackageDb &packages,
                  const std::vector<std::uint32_t> &users);

} // namespace tosya
