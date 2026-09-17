package dev.lightwolf.pumper.controller.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class EqMathTest {
    @Test
    fun `flat response and preamp remain zero`() {
        val curve = EqMath.responseCurve(DefaultEqConfig, 48_000)
        assertTrue(curve.all { kotlin.math.abs(it.gainDb) < 0.000001 })
        assertEquals(0.0, EqMath.calculateAutoPreamp(DefaultEqConfig, 48_000).preampDb, 0.000001)
    }

    @Test
    fun `auto preamp accounts for overlapping gains`() {
        val boosted = DefaultEqConfig.copy(
            bands = DefaultEqConfig.bands.mapIndexed { index, band ->
                if (index < 2) band.copy(type = FilterType.Peaking, widthMode = WidthMode.Q, frequencyHz = 1000.0, gainDb = 6.0, q = 1.0)
                else band
            },
        )
        val result = EqMath.calculateAutoPreamp(boosted, 48_000)
        assertTrue(result.peakDb > 11.9)
        assertTrue(result.preampDb < -11.9)
    }

    @Test
    fun `disabled EQ ignores preamp and filters`() {
        val config = DefaultEqConfig.copy(enabled = false, preampDb = 12.0)
        assertEquals(0.0, EqMath.compositeGainDb(config, 48_000, 1000.0), 0.0)
        assertEquals(0.0, EqMath.responseCurve(config, 48_000).first().gainDb, 0.0)
    }
}

