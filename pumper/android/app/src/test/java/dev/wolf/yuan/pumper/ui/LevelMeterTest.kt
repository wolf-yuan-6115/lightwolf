package dev.wolf.yuan.pumper.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LevelMeterTest {
    @Test
    fun `peak and mean square convert to dBFS`() {
        assertEquals(0f, peakToDb(32768), 0.001f)
        assertEquals(-6.0206f, peakToDb(16384), 0.001f)
        assertEquals(-3.0103f, meanSquareToDb(536_870_912), 0.001f)
        assertEquals(-60f, meanSquareToDb(0), 0f)
    }

    @Test
    fun `peak holds before decaying`() {
        val hold = PeakHoldState(1_000_000_000L)
        val current = MeterChannelModel(rmsDb = -10f, peakDb = -3f)
        val target = MeterChannelModel(rmsDb = -20f, peakDb = -18f)

        val held = advanceMeterChannel(current, target, 0.1f, 900_000_000L, hold)
        assertEquals(-3f, held.peakDb, 0f)

        val decaying = advanceMeterChannel(held, target, 0.1f, 1_100_000_000L, hold)
        assertEquals(-5.4f, decaying.peakDb, 0.001f)
        assertTrue(decaying.rmsDb < current.rmsDb)
    }
}
