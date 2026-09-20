package dev.wolf.yuan.pumper.protocol

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

    @Test
    fun `new RBJ filters have expected response at every supported rate`() {
        val rates = listOf(44_100L, 48_000L, 88_200L, 96_000L, 176_400L, 192_000L)
        rates.forEach { rate ->
            val lowPass = configWith(FilterType.LowPass, 1_000.0)
            val highPass = configWith(FilterType.HighPass, 1_000.0)
            val notch = configWith(FilterType.Notch, 1_000.0)
            val bandPass = configWith(FilterType.BandPass, 1_000.0)

            assertEquals(-3.01, EqMath.compositeGainDb(lowPass, rate, 1_000.0), 0.08)
            assertEquals(-3.01, EqMath.compositeGainDb(highPass, rate, 1_000.0), 0.08)
            assertTrue(EqMath.compositeGainDb(lowPass, rate, 100.0) > -0.1)
            assertTrue(EqMath.compositeGainDb(highPass, rate, 100.0) < -35.0)
            assertTrue(EqMath.compositeGainDb(notch, rate, 1_000.0) < -100.0)
            assertEquals(0.0, EqMath.compositeGainDb(bandPass, rate, 1_000.0), 0.0001)
            FilterType.entries.drop(3).forEach { type ->
                assertTrue(EqMath.responseCurve(configWith(type, 1_137.0), rate).all { it.gainDb.isFinite() })
            }
        }
    }

    @Test
    fun `new filters ignore gain but contribute to auto preamp and bypass at nyquist`() {
        val lowPass = configWith(FilterType.LowPass, 1_000.0, gainDb = 18.0)
        val neutralGain = configWith(FilterType.LowPass, 1_000.0, gainDb = 0.0)
        assertEquals(
            EqMath.compositeGainDb(neutralGain, 48_000, 4_000.0),
            EqMath.compositeGainDb(lowPass, 48_000, 4_000.0),
            0.000001,
        )
        assertTrue(EqMath.calculateAutoPreamp(configWith(FilterType.BandPass, 1_000.0), 48_000).peakDb > -0.01)
        assertEquals(0.0, EqMath.compositeGainDb(configWith(FilterType.LowPass, 24_000.0), 48_000, 8_000.0), 0.0)
    }

    private fun configWith(type: FilterType, frequencyHz: Double, gainDb: Double = 0.0): EqConfig = EqConfig(
        enabled = true,
        preampDb = 0.0,
        bands = listOf(
            EqBand(true, type, WidthMode.Q, frequencyHz, gainDb, 1.0 / kotlin.math.sqrt(2.0), 1.0),
        ),
    )
}
