// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <array>
#include <string_view>

namespace tosya {

// The packages HMA-OSS must never touch or hide: Constants.packagesShouldNotHide
// in the app. One copy here, for both the preset scanner (preset_rules.cpp) and
// the pair decisions (rules.cpp) -- two lists drifted apart is how the two halves
// end up disagreeing about what stays visible. Entries copied verbatim from the
// kReserved array this header replaces; regenerate by copying that array.
inline constexpr std::array<std::string_view, 14> kReservedPackages{
    std::string_view{"android"},
    std::string_view{"android.media"},
    std::string_view{"android.uid.system"},
    std::string_view{"android.uid.shell"},
    std::string_view{"android.uid.systemui"},
    std::string_view{"com.android.permissioncontroller"},
    std::string_view{"com.android.providers.downloads"},
    std::string_view{"com.android.providers.downloads.ui"},
    std::string_view{"com.android.providers.media"},
    std::string_view{"com.android.providers.media.module"},
    std::string_view{"com.android.providers.settings"},
    std::string_view{"com.google.android.providers.media.module"},
    std::string_view{"com.google.android.permissioncontroller"},
    std::string_view{"com.miui.securitycenter"},
};

} // namespace tosya
