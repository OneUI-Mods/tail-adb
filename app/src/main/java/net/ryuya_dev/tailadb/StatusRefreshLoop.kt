package net.ryuya_dev.tailadb

import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

internal class StatusRefreshCadence(private val baseMs: Long, private val maxMs: Long) {
    private var delayMs = baseMs

    fun reset() {
        delayMs = baseMs
    }

    fun completed(unchanged: Boolean): Long {
        delayMs = if (unchanged) (delayMs * 2).coerceAtMost(maxMs) else baseMs
        return delayMs
    }
}

/** Polls only while the activity is visible; real changes bring the next check forward. */
internal class StatusRefreshLoop(
    private val snapshot: () -> Any?,
    private val refresh: () -> Unit,
    private val executor: ScheduledExecutorService = Executors.newSingleThreadScheduledExecutor(),
    private val quickDelayMs: Long = 1000,
    private val baseDelayMs: Long = 3000,
    private val maxDelayMs: Long = 15000,
) : AutoCloseable {
    private val lock = Any()
    private var revision = 0L
    private var closed = false
    private var future: ScheduledFuture<*>? = null
    private var last: Any? = null
    private var hasSnapshot = false
    private val cadence = StatusRefreshCadence(baseDelayMs, maxDelayMs)

    fun changed() =
        synchronized(lock) {
            if (closed) return@synchronized
            revision++
            cadence.reset()
            future?.cancel(false)
            schedule(quickDelayMs)
        }

    private fun schedule(delay: Long) {
        val expected = revision
        future = executor.schedule({ poll(expected) }, delay, TimeUnit.MILLISECONDS)
    }

    private fun poll(expected: Long) {
        synchronized(lock) {
            if (closed || expected != revision) return
        }
        refresh()
        val value = snapshot()
        synchronized(lock) {
            // A broadcast can arrive during a slow root request. Its faster
            // check already owns the schedule; do not create a second loop.
            if (closed || expected != revision) return
            val delayMs = cadence.completed(hasSnapshot && value == last)
            last = value
            hasSnapshot = true
            schedule(delayMs)
        }
    }

    override fun close() {
        synchronized(lock) {
            closed = true
            revision++
            future?.cancel(true)
        }
        executor.shutdownNow()
    }
}
