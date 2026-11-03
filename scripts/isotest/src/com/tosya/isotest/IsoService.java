package com.tosya.isotest;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Process;
import android.util.Log;

public class IsoService extends Service {
  private static final String TAG = "isotest";

  @Override public IBinder onBind(Intent intent) { return null; }

  @Override public int onStartCommand(Intent intent, int flags, int startId) {
    Log.i(TAG, "up: uid=" + Process.myUid()
             + " pid=" + Process.myPid()
             + " ppid=" + readPpid()
             + " src=" + getApplicationInfo().sourceDir);

    try (java.io.InputStream in =
             new java.io.FileInputStream(getApplicationInfo().sourceDir)) {
      //noinspection ResultOfMethodCallIgnored
      in.read(new byte[64]);
    } catch (Exception e) {
      Log.i(TAG, "apk read: " + e);          // 失败不影响判据，见 ②
    }

    try { Thread.sleep(4000); } catch (InterruptedException ignored) { }
    stopSelf();
    return START_NOT_STICKY;
  }

  private static int readPpid() {
    try (java.io.BufferedReader r = new java.io.BufferedReader(
             new java.io.FileReader("/proc/self/status"))) {
      String l;
      while ((l = r.readLine()) != null)
        if (l.startsWith("PPid:")) return Integer.parseInt(l.substring(5).trim());
    } catch (Exception ignored) { }
    return -1;
  }
}
