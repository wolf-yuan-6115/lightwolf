package dev.wolf.yuan.pumper.protocol

import org.junit.Assert.*
import org.junit.Test

class EqTextTest {
    @Test fun roundTripsEveryFilterAndBandwidth() {
        val bands = FilterType.entries.map { type -> DefaultEqConfig.bands[0].copy(type = type, widthMode = WidthMode.Q, q = 0.7, gainDb = if (type.value <= 2) 3.0 else 0.0) } +
            DefaultEqConfig.bands[0].copy(type = FilterType.Peaking, widthMode = WidthMode.Bandwidth, bandwidthOctaves = 1.5)
        val config = EqConfig(false, -6.5, bands)
        val parsed = EqText.parse(EqText.serialize(config), bands.size)
        assertEquals(EqText.serialize(config), EqText.serialize(parsed))
    }
    @Test fun acceptsWebTextAndDisablesUnspecifiedBands() {
        val config = EqText.parse("# exported EQ\nPreamp: -4 dB\nFilter 2: ON PEQ Fc 1000 Hz Gain 3 dB Q 0.7")
        assertEquals(-4.0, config.preampDb, 0.0)
        assertFalse(config.bands[0].enabled)
        assertTrue(config.bands[1].enabled)
        assertEquals(FilterType.Peaking, config.bands[1].type)
    }
    @Test fun rejectsInvalidImports() {
        listOf("", "Preamp: 0 dB", "Filter 11: ON PK Fc 100 Hz Gain 0 dB Q 1", "Filter 1: ON LP Fc 100 Hz BW 1", "Filter 1: ON PK Fc 100 Hz Q 1", "Filter 1: ON PK Fc 100 Hz Gain 0 dB Q 99", "Filter 1: ON PK Fc 100 Hz Gain 0 dB Q 1\nFilter 1: ON PK Fc 200 Hz Gain 0 dB Q 1").forEach {
            assertTrue(it, runCatching { EqText.parse(it) }.isFailure)
        }
    }
}
