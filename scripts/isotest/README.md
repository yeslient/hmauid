# tosya isolated-process test app

A ~20-line app whose only purpose is to give the tosya tagging mechanism a
zygote-forked isolated process to name: the manifest declares one service with
`android:isolatedProcess="true"` (forked by the MAIN zygote, before == 0, so
the birth tag fires), the service opens its own base.apk once (so
`tosya_shadow_open()` can name it), then stops.

The WebView renderer path (webview_zygote, uid 1053) is structurally silent in
tosya's birth tag -- do not use a WebView app for this test; see
`docs/LIFECYCLE.md`, invariants (WZ).

## Files

- `AndroidManifest.xml`
- `src/com/tosya/isotest/IsoService.java`
- `build.sh`       -- Termux build: aapt + javac/d8 + apksigner
- `build-host.sh`  -- CI/PC build: pinned build-tools r34 + platform-34
  android.jar, downloaded on first use from dl.google.com; the URLs are the
  version pins, and both zips are Google's own repository artifacts.

## Build

Termux:

    pkg install aapt apksigner d8 ecj openjdk-21
    bash build.sh

CI/PC (needs a JDK 17+, curl, unzip; tools are fetched into `.scratch/`):

    bash build-host.sh

Produces `isotest.apk`, signed with a generated debug key.

## Run

    adb install isotest.apk
    # reboot once so the sync registers the apk (or wait for the next sync)
    adb shell am start-foreground-service -n com.tosya.isotest/.IsoService
    # (a plain startservice from shell hits the background-start restriction;
    #  the AMS resolve line in the error still proves the component is there)
    # then, within seconds:
    adb shell su -c "ps -A -o PID,PPID,NAME | grep 99"    # PPID = zygote64
    adb shell su -c "dmesg | grep -E 'iso (birth|uid)'"
    # expected two lines, same uid:
    #   tosya: iso birth uid 990xx marked, awaiting the apk it opens
    #   tosya: iso uid 990xx belongs to app <tosya.isotest appid>, from the apk it opened

## Provenance and license

Everything in this directory is original to this tree (GPL-2.0, like the
rest of the module): the manifest and the service were written for the
isolation-acceptance round, the build scripts bind the pinned Google
repository artifacts named above. The build downloads Google's build-tools
and platform zips subject to the Android SDK terms; no Google code is
checked in here.
