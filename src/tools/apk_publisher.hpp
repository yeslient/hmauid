// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <map>

#include "netlink.hpp"

namespace tosya {

enum class ApkDisplay { Unchanged, Refresh, Pending, Unreachable };

struct ApkPublishResult {
  bool complete;
  ApkDisplay display;
};

class ApkPublisher {
public:
  [[nodiscard]] ApkPublishResult publish(std::span<const ApkEntry> entries,
                                         NetlinkClient &client);

private:
  using ApkId = std::pair<std::uint32_t, std::uint64_t>;
  enum class ApkPhase { Confirmed, Adding, Dropping };
  struct PublishedApk {
    ApkEntry entry;
    ApkPhase phase = ApkPhase::Adding;
  };
  /* A lost ACK can hide a committed side effect. Adding remains possibly
   * installed; Dropping must finish before another ADD for the same identity.
   * Desired-set changes never discard either uncertainty. */
  std::map<ApkId, PublishedApk> published_;
};

} // namespace tosya
