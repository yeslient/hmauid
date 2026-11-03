#!/usr/bin/env bash
# Build isotest.apk with the standalone build tools -- the CI path and the PC
# path. The Termux variant is build.sh beside this file.
#
# The tools are pinned by URL and downloaded on first use:
#   build-tools r34  https://dl.google.com/android/repository/build-tools_r34-linux.zip
#   platform android.jar
#                    https://dl.google.com/android/repository/platform-34-ext7_r01.zip
# A debug keystore is generated on the spot; the app is a throwaway test
# subject, the signature only has to satisfy the package manager.
set -euo pipefail
cd "$(dirname "$0")"

TOOLS=${TOSYA_ISOTEST_TOOLS:-.scratch/isotest-tools}
BT="$TOOLS/bt34/android-14"
AJ="$TOOLS/android-14/android.jar"

if [ ! -f "$BT/aapt2" ] || [ ! -f "$AJ" ]; then
  echo "== fetching the pinned build tools =="
  mkdir -p "$TOOLS"
  curl -fsSL -o "$TOOLS/bt34.zip" \
    https://dl.google.com/android/repository/build-tools_r34-linux.zip
  curl -fsSL -o "$TOOLS/p34.zip" \
    https://dl.google.com/android/repository/platform-34-ext7_r01.zip
  unzip -q -o "$TOOLS/bt34.zip" -d "$TOOLS/bt34-unzip"
  unzip -q -o "$TOOLS/p34.zip" -d "$TOOLS/p34-unzip"
  # build-tools_r34-linux.zip unpacks one android-14/ directory; the platform
  # zip unpacks android-14/ alongside its other contents.
  mv "$TOOLS/bt34-unzip/android-14" "$TOOLS/bt34/android-14"
  mkdir -p "$TOOLS/android-14"
  find "$TOOLS/p34-unzip" -name android.jar -exec mv {} "$TOOLS/android-14/" \;
fi

KS="$TOOLS/debug.keystore"
[ -f "$KS" ] || keytool -genkeypair -v -keystore "$KS" -alias androiddebugkey \
  -keyalg RSA -keysize 2048 -validity 10000 -storepass android -keypass android \
  -dname "CN=Android Debug,O=Android,C=US" 2>/dev/null

rm -rf out && mkdir -p out

echo "== 1. aapt2 link (manifest only) =="
"$BT/aapt2" link -o out/unsigned.apk -I "$AJ" \
  --manifest AndroidManifest.xml \
  --min-sdk-version 26 --target-sdk-version 34

echo "== 2. javac =="
# --release 11: the r8.jar in use caps below class file v65 (Java 21), and the
# service needs nothing newer than Java 11 APIs anyway.
javac --release 11 -classpath "$AJ" -d out/classes \
  src/com/tosya/isotest/IsoService.java

echo "== 3. d8 (dex) =="
java -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 --release --min-api 26 \
  --lib "$AJ" --output out \
  $(find out/classes -name '*.class')

echo "== 4. pack classes.dex into the apk =="
cd out && jar uf unsigned.apk classes.dex && cd ..

echo "== 5. apksigner (v1+v2) =="
java -jar "$BT/lib/apksigner.jar" sign \
  --ks "$KS" --ks-key-alias androiddebugkey --ks-pass pass:android \
  --out isotest.apk out/unsigned.apk

java -jar "$BT/lib/apksigner.jar" verify --print-certs isotest.apk | head -3
echo "BUILD OK: $PWD/isotest.apk"
