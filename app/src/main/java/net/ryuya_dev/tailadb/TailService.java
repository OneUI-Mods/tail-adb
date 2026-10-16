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
              runCommand(true);
              Thread.sleep(300);
              State.set(this, "STARTING", "");
              runCommand(false);
            } else {
              runCommand(stop);
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

  private void runCommand(boolean stop) throws Exception {
    String folder = getApplicationInfo().nativeLibraryDir;
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
      var settings = getSharedPreferences("settings", MODE_PRIVATE);
      int port = settings.getInt("port", 5555);
      String allowed = settings.getString("allowed_hosts", "");
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
              + quote(allowed == null ? "" : allowed);
    }
    java.lang.Process process =
        new ProcessBuilder("su", "-c", command).redirectErrorStream(true).start();
    parse(process);
    if (!process.waitFor(30, TimeUnit.SECONDS)) {
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

  private void parse(java.lang.Process process) throws IOException {
    parse(this, process);
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
          State.setPeer(
              context,
              peer.length > 0 ? peer[0] : "",
              peer.length > 1 && peer[1].equals("ALLOWED"));
        } else if (fields[1].equals("PEER_CLOSED")) {
          State.setPeer(context, "", false);
        } else {
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
