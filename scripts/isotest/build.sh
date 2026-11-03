#!/data/data/com.termux/files/usr/bin/bash
# Build isotest.apk in Termux. Adjust the tool paths if your layout differs.
#   pkg install aapt apksigner d8 ecj openjdk-21
set -e
cd "$(dirname "$0")"

AAPT=${AAPT:-aapt}
DX=${DX:-d8}
APKSIGNER=${APKSIGNER:-apksigner}
# The android.jar bootclasspath: Termux's ecj/d8 need it for android.* types.
ANDROID_JAR=${ANDROID_JAR:-$PREFIX/share/android.jar}

rm -rf out && mkdir -p out/classes
"$AAPT" package -f -m -M AndroidManifest.xml -I "$ANDROID_JAR" -J out
javac -source 11 -target 11 -classpath "$ANDROID_JAR" -d out/classes \
  src/com/tosya/isotest/IsoService.java out/com/tosya/isotest/R.java
"$DX" --min-api 26 --output out/classes.dex out/classes
"$AAPT" package -f -M AndroidManifest.xml -I "$ANDROID_JAR" \
  -F out/isotest.unsigned
cd out && zip -q isotest.unsigned classes.dex && cd ..
"$APKSIGNER" sign --ks-source none --out isotest.apk out/isotest.unsigned \
  2>/dev/null || apksigner sign --ks-source none --out isotest.apk out/isotest.unsigned

echo "built: isotest.apk"
