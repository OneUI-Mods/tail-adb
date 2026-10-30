package net.ryuya_dev.tailadb

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import java.io.BufferedReader
import java.io.File
import java.io.IOException
import java.io.InputStreamReader
import java.util.concurrent.Callable
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory
import java.util.concurrent.TimeUnit
import kotlin.concurrent.Volatile
import kotlin.jvm.JvmField
import kotlin.jvm.JvmOverloads
import kotlin.jvm.JvmStatic

class TailService : Service() {
    private val work: ExecutorService? = Executors.newSingleThreadExecutor()

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (TailService.running) return Service.START_NOT_STICKY
        TailService.running = true
        val action: String? = if (intent == null) "" else intent.getAction()
        val stop: Boolean = "STOP" == action
        val restart: Boolean = "RESTART" == action
        val verify: Boolean = "VERIFY_NETWORK" == action
        if (!verify) State.set(this, if (stop || restart) "STOPPING" else "STARTING", "")
        work!!.execute(
            Runnable {
                try {
                    if (verify) {
                        val helper: String =
                            getApplicationInfo().nativeLibraryDir + "/libtailadb_exec.so"
                        val selectedUser =
                            getSharedPreferences("settings", Context.MODE_PRIVATE)
                                .getInt("tailscale_user", android.os.Process.myUid() / 100000)
                        val command: String =
                            "exec " + TailService.quote(helper) + " --network-check " + selectedUser
                        val process =
                            ProcessBuilder("su", "-c", command).redirectErrorStream(true).start()
                        TailService.await(this, process, 8)
                    } else if (restart) {
                        runBackendCommand(this, true)
                        Thread.sleep(300)
                        State.set(this, "STARTING", "")
                        runBackendCommand(this, false)
                    } else {
                        runBackendCommand(this, stop)
                    }
                    Thread.sleep(300)
                    TailService.refreshState(this)
                } catch (error: Throwable) {
                    State.set(this, "ERROR_PROCESS", error.javaClass.getSimpleName())
                } finally {
                    TailService.running = false
                    sendBroadcast(Intent(State.ACTION).setPackage(getPackageName()))
                    stopSelf(startId)
                }
            }
        )
        return Service.START_NOT_STICKY
    }

    override fun onDestroy() {
        work!!.shutdown()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    companion object {
        const val ROOT_DIRECTORY: String = "/data/local/tmp/net.ryuya_dev.tailadb"
        @JvmField @Volatile var running: Boolean = false

        @JvmStatic
        private fun quote(text: String): String {
            return "'" + text.replace("'", "'\\''") + "'"
        }

        @JvmStatic
        @JvmOverloads
        @Throws(Exception::class)
        fun runBackendCommand(context: Context, stop: Boolean, timeoutSeconds: Long = 30) {
            val folder = context.getApplicationInfo().nativeLibraryDir
            val helper = File(folder, "libtailadb_exec.so")
            if (!helper.canExecute()) throw IOException("Missing native backend")
            val command: String?
            if (stop) {
                command =
                    "exec " +
                        TailService.quote(helper.getAbsolutePath()) +
                        " --stop " +
                        TailService.quote(TailService.ROOT_DIRECTORY)
            } else {
                val shim = File(folder, "libtailadb_policy.so")
                val su = File(folder, "libtailadb_su.so")
                if (!shim.isFile() || !su.canExecute()) throw IOException("Missing native policy")
                val settings = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                val port = settings.getInt("port", 5555)
                val currentUser: Int = android.os.Process.myUid() / 100000
                val selectedUser = settings.getInt("tailscale_user", currentUser)
                val shellAllowed = settings.getString("shell_hosts", "")
                val rootAllowed = settings.getString("allowed_hosts", "")
                command =
                    ("exec " +
                        TailService.quote(helper.getAbsolutePath()) +
                        " --start " +
                        TailService.quote(shim.getAbsolutePath()) +
                        " " +
                        TailService.quote(su.getAbsolutePath()) +
                        " " +
                        TailService.quote(TailService.ROOT_DIRECTORY) +
                        " " +
                        port +
                        " " +
                        selectedUser +
                        " " +
                        TailService.quote(if (shellAllowed == null) "" else shellAllowed) +
                        " " +
                        TailService.quote(if (rootAllowed == null) "" else rootAllowed))
            }
            val process =
                (if (stop) ProcessBuilder("su", "-c", command)
                    else ProcessBuilder("su", "--ksu-no-new-privs", "-c", command))
                    .redirectErrorStream(true)
                    .start()
            TailService.await(context, process, timeoutSeconds)
        }

        @JvmStatic
        fun refreshState(context: Context) {
            try {
                val helper: String =
                    context.getApplicationInfo().nativeLibraryDir + "/libtailadb_exec.so"
                val command: String =
                    "exec " +
                        TailService.quote(helper) +
                        " --status " +
                        TailService.quote(TailService.ROOT_DIRECTORY)
                val process = ProcessBuilder("su", "-c", command).redirectErrorStream(true).start()
                TailService.await(context, process, 10)
            } catch (error: Throwable) {
                if (!TailService.running)
                    State.set(context, "ERROR_ROOT", error.javaClass.getSimpleName())
            }
        }

        @JvmStatic
        @Throws(Exception::class)
        private fun await(
            context: Context?,
            process: java.lang.Process,
            timeoutSeconds: Long,
        ) {
            val reader =
                Executors.newSingleThreadExecutor(
                    ThreadFactory { task: Runnable? ->
                        val thread = Thread(task, "TailAdbOutput")
                        thread.setDaemon(true)
                        thread
                    }
                )
            val parsed = reader.submit<Boolean?>(Callable { TailService.parse(context, process) })
            try {
                // Reading stdout first used to bypass waitFor's timeout when a root/Binder command
                // hung.
                if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS))
                    throw IOException("Root command timeout")
                val nativeError = parsed.get(2, TimeUnit.SECONDS)
                if (process.exitValue() != 0 && !nativeError)
                    throw IOException("Root command failed")
            } finally {
                process.destroyForcibly()
                parsed.cancel(true)
                reader.shutdownNow()
            }
        }

        @JvmStatic
        @Throws(IOException::class)
        private fun parse(context: Context?, process: java.lang.Process): Boolean {
            var nativeError: Boolean = false
            BufferedReader(InputStreamReader(process.getInputStream())).use { reader ->
                var line: String?
                while ((reader.readLine().also { line = it }) != null) {
                    if (!line!!.startsWith("TAILADB\t")) continue
                    val fields = line!!.split("\t".toRegex(), limit = 3).toTypedArray()
                    if (fields.size < 2 || !fields[1].matches("[A-Z_]{2,40}".toRegex())) continue
                    nativeError = nativeError or fields[1].startsWith("ERROR_")
                    if (fields[1] == "NETWORK_CHECK") {
                        State.setNetworkCheck(context!!, if (fields.size > 2) fields[2] else "")
                    } else if (fields[1] == "PEER") {
                        val peer: Array<String?>? =
                            if (fields.size > 2)
                                fields[2].split("\t".toRegex(), limit = 2).toTypedArray()
                            else arrayOfNulls<String>(0)
                        State.setPeer(context!!, if (peer!!.size > 0) peer!![0] else "")
                    } else if (fields[1] == "PEER_CLOSED") {
                        State.setPeer(context!!, "")
                    } else {
                        if (
                            fields[1] == "STOPPED" ||
                                fields[1].startsWith("ERROR_") ||
                                fields[1].startsWith("WAITING_")
                        )
                            State.setPeer(context!!, "")
                        State.set(context!!, fields[1], if (fields.size > 2) fields[2] else "")
                    }
                }
            }
            return nativeError
        }
    }
}
