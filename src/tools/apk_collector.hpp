// SPDX-License-Identifier: GPL-2.0
#pragma once

#include "netlink.hpp"
#include "packages.hpp"

namespace tosya {

/* Userspace scan bound, independent of the kernel's allocated shadow records.
 */
inline constexpr std::size_t kApkCollectionLimit = 10000;

/* Caller paths precede other apps. Existing early install hints survive until
 * Syncer accepts a new package snapshot and clears its path cache. */
[[nodiscard]] std::vector<ApkEntry> collect_apks(
    const PackageDb *packages,
    const std::map<std::string, std::uint32_t, std::less<>> &callers,
    std::map<std::string, std::filesystem::path, std::less<>> &code_dirs);

} // namespace tosya
