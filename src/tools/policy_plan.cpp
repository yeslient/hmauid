// SPDX-License-Identifier: GPL-2.0
#include "policy_plan.hpp"

#include <algorithm>

namespace tosya {

std::expected<PolicyPlan, std::size_t>
build_policy_plan(const Rules &rules, const Presets &presets,
                  const PackageDb &packages,
                  const std::vector<std::uint32_t> &users) {
  const Pairs base_pairs = rules.expand(packages, presets);
  PolicyPlan plan;
  plan.pairs = expand_users(base_pairs, users);
  std::ranges::sort(plan.pairs, [](const Pair &a, const Pair &b) {
    return a.caller != b.caller ? a.caller < b.caller : a.target < b.target;
  });
  plan.pairs.erase(std::unique(plan.pairs.begin(), plan.pairs.end()),
                   plan.pairs.end());
  if (plan.pairs.size() > NetlinkClient::kMaxPairs)
    return std::unexpected(plan.pairs.size());

  // Names use app ids before user expansion; the kernel folds callers to app
  // ids.
  std::map<std::uint32_t, std::vector<std::string>> names_of_uid;
  for (const auto &[name, info] : packages.by_name())
    names_of_uid[info.uid].emplace_back(name);
  std::set<std::pair<std::uint32_t, std::string>> seen;
  plan.names.reserve(base_pairs.size());
  for (const auto &pair : base_pairs) {
    const auto ns = names_of_uid.find(pair.target);
    if (ns == names_of_uid.end())
      continue;
    for (const auto &name : ns->second) {
      if (!seen.emplace(pair.caller, name).second)
        continue;
      plan.names.push_back(HiddenName{.caller = pair.caller, .name = name});
    }
  }

  std::set<std::uint32_t> caller_uids;
  bool wild = false;
  for (const auto &pair : plan.pairs) {
    if (pair.caller == 0)
      wild = true;
    else
      caller_uids.insert(pair.caller);
  }
  for (const auto &[name, info] : packages.by_name()) {
    if (wild || caller_uids.contains(info.uid))
      plan.callers.emplace(name, info.uid);
  }
  return plan;
}

} // namespace tosya
