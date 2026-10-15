package net.ryuya_dev.tailadb;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.*;
import androidx.core.app.NotificationCompat;
import java.io.*;
import java.util.concurrent.*;

public final class TailService extends Service {
  static volatile boolean running;
  private static final int NOTIFICATION = 1;
  private final ExecutorService work = Executors.newSingleThreadExecutor();
  private volatile java.lang.Process process;
  private volatile boolean stopping;
  private NotificationManager notifications;

  @Override
  public void onCreate() {
    super.onCreate();
    notifications = getSystemService(NotificationManager.class);
    notifications.createNotificationChannel(
        new NotificationChannel(
            "connection", getString(R.string.channel), NotificationManager.IMPORTANCE_LOW));
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    if (intent != null && "STOP".equals(intent.getAction())) {
      stopBackend();
      return START_NOT_STICKY;
    }
    if (running) return START_NOT_STICKY;
    running = true;
    stopping = false;
    startForeground(
        NOTIFICATION, notification("STARTING"), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
    State.set(this, "STARTING", "");
    int port = getSharedPreferences("settings", MODE_PRIVATE).getInt("port", 5555);
    work.execute(() -> runBackend(port));
    return START_NOT_STICKY;
  }

  private static String quote(String text) {
    return "'" + text.replace("'", "'\\''") + "'";
  }

  private void runBackend(int port) {
    boolean sawState = false, error = false;
    try {
      String folder = getApplicationInfo().nativeLibraryDir;
      File helper = new File(folder, "libtailadb_exec.so"),
          shim = new File(folder, "libtailadb_policy.so");
      if (!helper.canExecute() || !shim.isFile()) throw new IOException("Missing native backend");
      String command =
          "exec "
              + quote(helper.getAbsolutePath())
              + " "
              + quote(shim.getAbsolutePath())
              + " /data/local/tmp/net.ryuya_dev.tailadb "
              + port;
      process = new ProcessBuilder("su", "-c", command).redirectErrorStream(true).start();
      if (stopping) process.getOutputStream().close();
      try (BufferedReader reader =
          new BufferedReader(new InputStreamReader(process.getInputStream()))) {
        String line;
        while ((line = reader.readLine()) != null) {
          if (!line.startsWith("TAILADB\t")) continue;
          String[] fields = line.split("\t", 3);
          if (fields.length < 2) continue;
          String state = fields[1];
          if (!state.matches("[A-Z_]{2,40}")) continue;
          sawState = true;
          if (state.startsWith("ERROR_")) error = true;
          if (state.equals("STOPPED") && error) continue;
          State.set(this, state, fields.length > 2 ? fields[2] : "");
          notifications.notify(NOTIFICATION, notification(state));
          android.util.Log.i("TailAdb", state);
        }
      }
      int code = process.waitFor();
      if (!error)
        State.set(
            this, stopping ? "STOPPED" : (sawState && code == 0 ? "STOPPED" : "ERROR_ROOT"), "");
    } catch (Throwable e) {
      State.set(this, stopping ? "STOPPED" : "ERROR_PROCESS", e.getClass().getSimpleName());
    } finally {
      process = null;
      running = false;
      stopForeground(STOP_FOREGROUND_REMOVE);
      stopSelf();
    }
  }

  private Notification notification(String state) {
    PendingIntent open =
        PendingIntent.getActivity(
            this,
            0,
            new Intent(this, MainActivity.class),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    PendingIntent stop =
        PendingIntent.getService(
            this,
            1,
            new Intent(this, TailService.class).setAction("STOP"),
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    return new NotificationCompat.Builder(this, "connection")
        .setSmallIcon(R.drawable.ic_terminal)
        .setContentTitle(getString(R.string.notification))
        .setContentText(getString(State.label(state)))
        .setContentIntent(open)
        .setOngoing(true)
        .setOnlyAlertOnce(true)
        .addAction(0, getString(R.string.stop), stop)
        .build();
  }

  private void stopBackend() {
    stopping = true;
    State.set(this, "STOPPING", "");
    java.lang.Process current = process;
    if (current != null) {
      try {
        current.getOutputStream().write('q');
        current.getOutputStream().flush();
        current.getOutputStream().close();
      } catch (IOException ignored) {
        current.destroy();
      }
    } else if (!running) stopSelf();
  }

  @Override
  public void onDestroy() {
    if (running) stopBackend();
    work.shutdown();
    super.onDestroy();
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }
}
