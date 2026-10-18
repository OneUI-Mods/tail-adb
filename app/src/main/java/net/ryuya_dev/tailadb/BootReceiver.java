package net.ryuya_dev.tailadb;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public final class BootReceiver extends BroadcastReceiver {
  private static final String PREFERENCES = "boot_settings";
  private static final String ENABLED = "auto_start";

  static boolean isEnabled(Context context) {
    return context
        .createDeviceProtectedStorageContext()
        .getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        .getBoolean(ENABLED, false);
  }

  static void setEnabled(Context context, boolean enabled) {
    context
        .createDeviceProtectedStorageContext()
        .getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        .edit()
        .putBoolean(ENABLED, enabled)
        .apply();
  }

  @Override
  public void onReceive(Context context, Intent intent) {
    if (intent == null
        || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())
        || !isEnabled(context)) return;
    PendingResult pending = goAsync();
    Context application = context.getApplicationContext();
    new Thread(
            () -> {
              try {
                State.set(application, "AUTO_STARTING", "");
                TailService.runBackendCommand(application, false, 8);
                Thread.sleep(300);
                TailService.refreshState(application);
              } catch (Throwable error) {
                State.set(application, "ERROR_AUTO_ROOT", error.getClass().getSimpleName());
              } finally {
                pending.finish();
              }
            },
            "TailAdbBoot")
        .start();
  }
}
