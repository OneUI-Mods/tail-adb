package net.ryuya_dev.tailadb

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.*
import org.junit.Test

class StatusRefreshLoopTest {
    @Test
    fun stableStatesBackOffAndChangesResetTheCadence() {
        val cadence = StatusRefreshCadence(3000, 15000)
        assertEquals(3000L, cadence.completed(false))
        assertEquals(
            listOf(6000L, 12000L, 15000L, 15000L),
            List(4) { cadence.completed(true) },
        )
        assertEquals(3000L, cadence.completed(false))
        cadence.completed(true)
        cadence.reset()
        assertEquals(6000L, cadence.completed(true))
    }

    @Test
    fun changesDuringRefreshDoNotCreateParallelOrDuplicateLoops() {
        val calls = AtomicInteger()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val second = CountDownLatch(1)
        val loop =
            StatusRefreshLoop(
                snapshot = { "unchanged" },
                refresh = {
                    when (calls.incrementAndGet()) {
                        1 -> {
                            entered.countDown()
                            assertTrue(release.await(3, TimeUnit.SECONDS))
                        }
                        2 -> second.countDown()
                        else -> fail("Duplicate polling loop")
                    }
                },
                quickDelayMs = 1,
                baseDelayMs = 60000,
                maxDelayMs = 60000,
            )
        try {
            loop.changed()
            assertTrue(entered.await(3, TimeUnit.SECONDS))
            repeat(100) { loop.changed() }
            assertEquals(1, calls.get())
            release.countDown()
            assertTrue(second.await(3, TimeUnit.SECONDS))
        } finally {
            release.countDown()
            loop.close()
        }
        loop.changed() // No new work or rejected task after the activity stops.
        assertEquals(2, calls.get())
    }
}
