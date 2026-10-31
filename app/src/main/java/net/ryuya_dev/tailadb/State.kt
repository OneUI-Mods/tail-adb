package net.ryuya_dev.tailadb

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.os.SystemClock
import kotlin.jvm.JvmStatic
import kotlin.math.min

object State {
    const val ACTION: String = "net.ryuya_dev.tailadb.STATE"

    @JvmStatic
    fun prefs(c: Context): SharedPreferences? {
        return c.getSharedPreferences("state", Context.MODE_PRIVATE)
    }

    @JvmStatic
    fun set(c: Context, value: String?, detail: String?) {
        State.prefs(c)!!.edit()
            .putString("state", value)
            .putString(
                "detail",
                if (detail == null) "" else detail.substring(0, min(1000, detail.length)),
            )
            .apply()
        c.sendBroadcast(Intent(State.ACTION).setPackage(c.getPackageName()))
    }

    @JvmStatic
    fun setPeer(c: Context, address: String?) {
        State.prefs(c)!!.edit()
            .putString("peer", if (address == null) "" else address)
            .remove("peer_allowed")
            .apply()
        c.sendBroadcast(Intent(State.ACTION).setPackage(c.getPackageName()))
    }

    @JvmStatic
    fun setNetworkCheck(context: Context, detail: String) {
        val values = detail.split("\t".toRegex()).toTypedArray()
        if (
            values.size < 2 ||
                !values[1].matches("[0-9]{1,5}".toRegex()) ||
                !java.util.Set.of<String?>(
                        "READY",
                        "OTHER_USER",
                        "MULTIPLE",
                        "INSPECTION_ERROR",
                        "UNAVAILABLE",
                    )
                    .contains(values[0])
        )
            return
        State.prefs(context)!!.edit()
            .putString("network_state", values[0])
            .putInt("network_user", values[1].toInt())
            .putLong("network_time", SystemClock.elapsedRealtime())
            .apply()
        context.sendBroadcast(Intent(State.ACTION).setPackage(context.getPackageName()))
    }

    @JvmStatic
    fun label(state: String?): Int {
        return when (state) {
            "LISTENING" -> R.string.state_listening
            "STARTING" -> R.string.state_starting
            "AUTO_STARTING" -> R.string.state_auto_starting
            "STOPPING" -> R.string.state_stopping
            "WAITING_TAILSCALE" -> R.string.state_waiting
            "WAITING_SELECTED_USER" -> R.string.state_selected_user
            "WAITING_MULTIPLE_TAILSCALE" -> R.string.state_multiple_tailscale
            "WAITING_NETWORK_INSPECTION" -> R.string.state_network_inspection
            "WAITING_NETWORK_RECOVERY" -> R.string.state_network_recovery
            "RETRYING" -> R.string.state_retrying
            "ERROR_ROOT" -> R.string.state_root
            "ERROR_COMPAT" -> R.string.state_compat
            "ERROR_ISOLATION" -> R.string.state_isolation
            "ERROR_BIND" -> R.string.state_bind
            "ERROR_CRASH_LOOP" -> R.string.state_crashes
            "ERROR_ALREADY_RUNNING" -> R.string.state_running
            "ERROR_BROKER",
            "ERROR_CONTROL" -> R.string.state_security
            "ERROR_AUTO_ROOT" -> R.string.state_auto_root
            "STOPPED" -> R.string.state_stopped
            else -> R.string.state_error
        }
    }
}
