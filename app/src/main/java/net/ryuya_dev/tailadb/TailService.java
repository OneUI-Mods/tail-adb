package net.ryuya_dev.tailadb;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import java.io.*;
import java.util.concurrent.*;

public final class TailService extends Service {
  static final String ROOT_DIRECTORY = "/data/local/tmp/net.ryuya_dev.tailadb";
  static volatile boolean running;
  private final ExecutorService work = Executors.newSingleThreadExecutor();

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    if (running) return START_NOT_STICKY;
    running = true;
    String action = intent == null ? "" : intent.getAction();
    boolean stop = "STOP".equals(action), restart = "RESTART".equals(action);
    State.set(this, stop || restart ? "STOPPING" : "STARTING", "");
    work.execute(
        () -> {
          try {
            if (restart) {
              runBackendCommand(this, true);
              Thread.sleep(300);
              State.set(this, "STARTING", "");
              runBackendCommand(this, false);
            } else {
              runBackendCommand(this, stop);
            }
            Thread.sleep(300);
            refreshState(this);
          } catch (Throwable error) {
            State.set(this, "ERROR_PROCESS", error.getClass().getSimpleName());
          } finally {
            running = false;
            stopSelf(startId);
          }
        });
    return START_NOT_STICKY;
  }

  private static String quote(String text) {
    return "'" + text.replace("'", "'\\''") + "'";
  }

  static void runBackendCommand(android.content.Context context, boolean stop) throws Exception {
    runBackendCommand(context, stop, 30);
  }

  static void runBackendCommand(
      android.content.Context context, boolean stop, long timeoutSeconds) throws Exception {
    String folder = context.getApplicationInfo().nativeLibraryDir;
    File helper = new File(folder, "libtailadb_exec.so");
    if (!helper.canExecute()) throw new IOException("Missing native backend");
    String command;
    if (stop) {
      command =
          "exec " + quote(helper.getAbsolutePath()) + " --stop " + quote(ROOT_DIRECTORY);
    } else {
      File shim = new File(folder, "libtailadb_policy.so");
      File su = new File(folder, "libtailadb_su.so");
      if (!shim.isFile() || !su.canExecute()) throw new IOException("Missing native policy");
      var settings = context.getSharedPreferences("settings", MODE_PRIVATE);
      int port = settings.getInt("port", 5555);
      int currentUser = android.os.Process.myUid() / 100000;
      int selectedUser = settings.getInt("tailscale_user", currentUser);
      String shellAllowed = settings.getString("shell_hosts", "");
      String rootAllowed = settings.getString("allowed_hosts", "");
      command =
          "exec "
              + quote(helper.getAbsolutePath())
              + " --start "
              + quote(shim.getAbsolutePath())
              + " "
              + quote(su.getAbsolutePath())
              + " "
              + quote(ROOT_DIRECTORY)
              + " "
              + port
              + " "
              + selectedUser
              + " "
              + quote(shellAllowed == null ? "" : shellAllowed)
              + " "
              + quote(rootAllowed == null ? "" : rootAllowed);
    }
    java.lang.Process process =
        (stop
                ? new ProcessBuilder("su", "-c", command)
                : new ProcessBuilder("su", "--ksu-no-new-privs", "-c", command))
            .redirectErrorStream(true)
            .start();
    parse(context, process);
    if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) {
      process.destroyForcibly();
      throw new IOException("Root command timeout");
    }
    if (process.exitValue() != 0) throw new IOException("Root command failed");
  }

  static void refreshState(android.content.Context context) {
    try {
      String helper = context.getApplicationInfo().nativeLibraryDir + "/libtailadb_exec.so";
      String command = "exec " + quote(helper) + " --status " + quote(ROOT_DIRECTORY);
      java.lang.Process process =
          new ProcessBuilder("su", "-c", command).redirectErrorStream(true).start();
      parse(context, process);
      if (!process.waitFor(10, TimeUnit.SECONDS)) process.destroyForcibly();
    } catch (Throwable error) {
      if (!running) State.set(context, "ERROR_ROOT", error.getClass().getSimpleName());
    }
  }

  private static void parse(android.content.Context context, java.lang.Process process)
      throws IOException {
    try (BufferedReader reader =
        new BufferedReader(new InputStreamReader(process.getInputStream()))) {
      String line;
      while ((line = reader.readLine()) != null) {
        if (!line.startsWith("TAILADB\t")) continue;
        String[] fields = line.split("\t", 3);
        if (fields.length < 2 || !fields[1].matches("[A-Z_]{2,40}")) continue;
        if (fields[1].equals("PEER")) {
          String[] peer = fields.length > 2 ? fields[2].split("\t", 2) : new String[0];
          State.setPeer(context, peer.length > 0 ? peer[0] : "");
        } else if (fields[1].equals("PEER_CLOSED")) {
          State.setPeer(context, "");
        } else {
          if (fields[1].equals("STOPPED") || fields[1].startsWith("ERROR_") ||
              fields[1].startsWith("WAITING_")) State.setPeer(context, "");
          State.set(context, fields[1], fields.length > 2 ? fields[2] : "");
        }
      }
    }
  }

  @Override
  public void onDestroy() {
    work.shutdown();
    super.onDestroy();
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }
}
