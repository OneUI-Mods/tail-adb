package net.ryuya_dev.tailadb;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

final class State {
  static final String ACTION = "net.ryuya_dev.tailadb.STATE";

  static SharedPreferences prefs(Context c) {
    return c.getSharedPreferences("state", Context.MODE_PRIVATE);
  }

  static void set(Context c, String value, String detail) {
    prefs(c)
        .edit()
        .putString("state", value)
        .putString(
            "detail", detail == null ? "" : detail.substring(0, Math.min(1000, detail.length())))
        .apply();
    c.sendBroadcast(new Intent(ACTION).setPackage(c.getPackageName()));
  }

  static void setPeer(Context c, String address) {
    prefs(c)
        .edit()
        .putString("peer", address == null ? "" : address)
        .remove("peer_allowed")
        .apply();
    c.sendBroadcast(new Intent(ACTION).setPackage(c.getPackageName()));
  }

  static int label(String state) {
    return switch (state) {
      case "LISTENING" -> R.string.state_listening;
      case "STARTING" -> R.string.state_starting;
      case "AUTO_STARTING" -> R.string.state_auto_starting;
      case "STOPPING" -> R.string.state_stopping;
      case "WAITING_TAILSCALE" -> R.string.state_waiting;
      case "WAITING_SELECTED_USER" -> R.string.state_selected_user;
      case "WAITING_MULTIPLE_TAILSCALE" -> R.string.state_multiple_tailscale;
      case "WAITING_NETWORK_INSPECTION" -> R.string.state_network_inspection;
      case "RETRYING" -> R.string.state_retrying;
      case "ERROR_ROOT" -> R.string.state_root;
      case "ERROR_COMPAT" -> R.string.state_compat;
      case "ERROR_ISOLATION" -> R.string.state_isolation;
      case "ERROR_BIND" -> R.string.state_bind;
      case "ERROR_CRASH_LOOP" -> R.string.state_crashes;
      case "ERROR_ALREADY_RUNNING" -> R.string.state_running;
      case "ERROR_BROKER", "ERROR_CONTROL" -> R.string.state_security;
      case "ERROR_AUTO_ROOT" -> R.string.state_auto_root;
      case "STOPPED" -> R.string.state_stopped;
      default -> R.string.state_error;
    };
  }
}
