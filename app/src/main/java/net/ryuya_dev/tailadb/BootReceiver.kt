package net.ryuya_dev.tailadb

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import kotlin.jvm.JvmStatic

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (
            intent == null ||
                (Intent.ACTION_BOOT_COMPLETED != intent.getAction()) ||
                !BootReceiver.isEnabled(context)
        )
            return
        val pending = goAsync()
        val application = context.getApplicationContext()
        Thread(
                Runnable {
                    try {
                        State.set(application, "AUTO_STARTING", "")
                        TailService.runBackendCommand(application, false, 8)
                        Thread.sleep(300)
                        TailService.refreshState(application)
                    } catch (error: Throwable) {
                        State.set(application, "ERROR_AUTO_ROOT", error.javaClass.getSimpleName())
                    } finally {
                        pending.finish()
                    }
                },
                "TailAdbBoot",
            )
            .start()
    }

    companion object {
        private const val PREFERENCES: String = "boot_settings"
        private const val ENABLED: String = "auto_start"

        @JvmStatic
        fun isEnabled(context: Context): Boolean {
            return context
                .createDeviceProtectedStorageContext()
                .getSharedPreferences(BootReceiver.PREFERENCES, Context.MODE_PRIVATE)
                .getBoolean(BootReceiver.ENABLED, false)
        }

        @JvmStatic
        fun setEnabled(context: Context, enabled: Boolean) {
            context
                .createDeviceProtectedStorageContext()
                .getSharedPreferences(BootReceiver.PREFERENCES, Context.MODE_PRIVATE)
                .edit()
                .putBoolean(BootReceiver.ENABLED, enabled)
                .apply()
        }
    }
}
