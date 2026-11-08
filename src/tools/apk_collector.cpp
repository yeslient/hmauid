// SPDX-License-Identifier: GPL-2.0
#include "apk_collector.hpp"

#include <set>
#include <sys/stat.h>

namespace tosya {
namespace {
/* The layouts that name their apk something no stat guess can predict: an
 * apex module (Foo@1.2.3/Foo.apk), an overlay (Name/NameOverlay.apk), a
 * preinstalled app shipped under its own file name (MIUIScreenRecorder.apk).
 * One readdir answers all three, but only when the choice is unambiguous:
 * exactly one apk, or a base.apk among several. A split-only directory has
 * several parts and no base; naming the package after one part would put the
 * wrong inode in the table, so that one stays unregistered as before. */
[[nodiscard]] std::filesystem::path apk_in(const std::filesystem::path &dir) {
  std::error_code ec;
  std::filesystem::directory_iterator it{dir, ec};
  if (ec)
    return {};
  std::filesystem::path first;
  std::filesystem::path base;
  std::size_t count = 0;
  for (const auto &entry : it) {
    if (entry.path().extension() != ".apk")
      continue;
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || entry_ec)
      continue;
    ++count;
    if (count == 1)
      first = entry.path();
    if (entry.path().filename() == "base.apk")
      base = entry.path();
  }
  if (!base.empty())
    return base;
  if (count == 1)
    return first;
  return {};
}

} // namespace

std::vector<ApkEntry> collect_apks(
    const PackageDb *packages,
    const std::map<std::string, std::uint32_t, std::less<>> &callers,
    std::map<std::string, std::filesystem::path, std::less<>> &code_dirs) {
  std::vector<ApkEntry> entries;
  entries.reserve(callers.size());
  /* One entry per directory, even when several packages share a uid. */
  /* One entry per apk file, even when several packages share a code dir. */
  std::set<std::string, std::less<>> seen;

  /*
   * Every installed app, not only the callers: the table is what lets the
   * kernel name an isolated process by the apk it opens, and its host is
   * whatever app spawned it -- a WebView renderer, a renderer service,
   * anything. The callers go in first, so a table that hits the cap still
   * carries the ones the policy needs.
   */
  std::vector<std::pair<std::string, std::uint32_t>> wanted;
  wanted.reserve(packages ? packages->by_name().size() : callers.size());
  for (const auto &[name, uid] : callers)
    wanted.emplace_back(name, uid);
  if (packages) {
    for (const auto &[name, info] : packages->by_name()) {
      if (info.uid < kFirstAppUid || callers.contains(name))
        continue;
      wanted.emplace_back(name, info.uid);
    }
  }

  std::size_t stat_missed = 0;
  for (const auto &[name, uid] : wanted) {
    auto dir = code_dirs.find(name);
    if (dir == code_dirs.end() && packages) {
      /* Not a caller: its directory comes straight from the package database.
       */
      if (const auto own = packages->code_dir_of(name)) {
        code_dirs.insert_or_assign(name, *own);
        dir = code_dirs.find(name);
      }
    }
    if (dir == code_dirs.end())
      continue;
    /*
     * The file a child opens first is its own base.apk, and that is the inode
     * whose open the kernel replaces, so the path to it is what goes up. A
     * system app's apk is under /system, an installed one's under /data; both
     * are files with an inode, and which partition it is on no longer matters.
     */
    /* packages.xml also carries a direct Foo.apk path for system packages.
     * Such a path must not become Foo.apk/base.apk. And many system apps name
     * the file after the directory instead of base.apk
     * (Settings/Settings.apk) -- the shipped base.apk-only fallback left
     * every one of them unregistered. */
    std::error_code ec;
    auto apk = dir->second;
    if (!std::filesystem::is_regular_file(apk, ec)) {
      const auto base = apk / "base.apk";
      /* Append, not replace_extension: directory names carry dots
       * (com.xiaomi.macro), and replace_extension would cut the last one. */
      const auto named = apk / (apk.filename().string() + ".apk");
      if (std::filesystem::is_regular_file(base, ec))
        apk = base;
      else if (std::filesystem::is_regular_file(named, ec))
        apk = named;
      else
        apk = apk_in(apk);
    }
    /* A directory that still resolves to no apk -- split-only code paths,
     * and directories that vanished between the database read and now. The
     * path to a DIRECTORY is what stat() would happily accept, and the
     * kernel's sweep refuses to arm a directory inode, so a directory entry
     * here is a guaranteed "could not arm" per sweep: skip it with the
     * rest. */
    if (!std::filesystem::is_regular_file(apk, ec)) {
      ++stat_missed;
      continue;
    }
    if (!seen.insert(apk.string()).second)
      continue;
    struct stat info{};
    if (::stat(apk.c_str(), &info) != 0) {
      /* Not fatal for the batch, but silent would mean an identity that never
       * comes back: say it, so a missing registration can be explained. */
      ++stat_missed;
      continue;
    }
    if (entries.size() >= kApkCollectionLimit)
      break;
    entries.push_back(ApkEntry{.path = apk.string(),
                               .uid = uid,
                               .dev = static_cast<std::uint32_t>(info.st_dev),
                               .ino = static_cast<std::uint64_t>(info.st_ino)});
  }
  if (stat_missed != 0)
    Log::warn("{} apk file(s) could not be stat'ed; they stay unregistered "
              "until a later sync re-reads them",
              stat_missed);

  return entries;
}

} // namespace tosya
