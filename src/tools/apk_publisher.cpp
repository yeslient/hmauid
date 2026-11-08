// SPDX-License-Identifier: GPL-2.0
#include "apk_publisher.hpp"

namespace tosya {

ApkPublishResult ApkPublisher::publish(std::span<const ApkEntry> entries,
                                       NetlinkClient &client) {
  const auto id_of = [](const ApkEntry &entry) {
    return ApkId{entry.dev, entry.ino};
  };
  std::map<ApkId, ApkEntry> desired;
  for (const auto &entry : entries)
    desired.try_emplace(id_of(entry), entry);

  bool recovering = false;
  std::vector<ApkEntry> drops;
  for (auto &[id, old] : published_) {
    recovering |= old.phase == ApkPhase::Adding;
    const auto want = desired.find(id);
    if (old.phase == ApkPhase::Dropping || want == desired.end() ||
        want->second.uid != old.entry.uid) {
      auto entry = old.entry;
      entry.action = 1;
      drops.push_back(std::move(entry));
      old.phase = ApkPhase::Dropping;
    }
  }
  /* Drops and adds are separate batches. A valid drop cannot fail per entry;
   * its ACK confirms absence. If the reply is lost, repeat only drops before
   * any add: replaying a mixed batch could park its own newly installed UID. */
  if (!drops.empty()) {
    if (!client.push_apks(drops)) {
      return {false, ApkDisplay::Unchanged};
    }
    for (const auto &entry : drops)
      published_.erase(id_of(entry));
  }

  std::vector<ApkEntry> adds;
  for (const auto &[id, entry] : desired) {
    const auto have = published_.find(id);
    if (have == published_.end() || have->second.phase == ApkPhase::Adding)
      adds.push_back(entry);
  }
  Log::info("registered {} app apk(s), {} change(s) to send", desired.size(),
            drops.size() + adds.size());
  if (drops.empty() && adds.empty())
    return {true, ApkDisplay::Unchanged};

  enum class AddResult { Complete, Failed, Unreachable };
  const auto add = [&](std::span<const ApkEntry> batch) {
    /* Record possible effects before sending, including a commit whose ACK
     * is lost. A desired-set rollback must still retire these identities. */
    for (const auto &entry : batch)
      published_.insert_or_assign(id_of(entry),
                                  PublishedApk{entry, ApkPhase::Adding});
    if (!client.push_apks(batch))
      /* A refusal means the kernel is there and said no -- later entries in
       * the recovering loop still get their own attempt. Calling it
       * unreachable would starve everything behind the first refusal. */
      return client.refused() ? AddResult::Failed : AddResult::Unreachable;
    const auto st = client.status();
    if (!st || st->apk_offered != batch.size())
      return AddResult::Unreachable;
    if (st->apk_failed)
      return AddResult::Failed;
    for (const auto &entry : batch)
      published_.at(id_of(entry)).phase = ApkPhase::Confirmed;
    return AddResult::Complete;
  };

  bool complete = true;
  bool reachable = true;
  if (recovering) {
    /* A failed batch says nothing about which additions succeeded. Re-adds
     * are idempotent after their drop barrier; confirm them individually so
     * one bad inode does not keep every successful entry in the retry set. */
    for (const auto &entry : adds) {
      const auto result = add(std::span<const ApkEntry>(&entry, 1));
      if (result != AddResult::Complete)
        complete = false;
      if (result == AddResult::Unreachable) {
        reachable = false;
        break;
      }
    }
  } else if (!adds.empty()) {
    const auto result = add(adds);
    complete = result == AddResult::Complete;
    reachable = result != AddResult::Unreachable;
  }
  return {complete, !reachable ? ApkDisplay::Unreachable
                    : complete ? ApkDisplay::Refresh
                               : ApkDisplay::Pending};
}

} // namespace tosya
