package dev.lightwolf.pumper.controller.protocol

import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class EqValidationTest {
    @Test
    fun `new filters require Q and accept full Q range`() {
        listOf(FilterType.LowPass, FilterType.HighPass, FilterType.Notch, FilterType.BandPass).forEach { type ->
            val band = EqBand(true, type, WidthMode.Q, 1_000.0, 12.0, 20.0, 1.0)
            assertNull(EqValidation.band(band, 0))
            assertNotNull(EqValidation.band(band.copy(widthMode = WidthMode.Bandwidth), 0))
            assertNotNull(EqValidation.band(band.copy(q = 20.01), 0))
        }
    }

    @Test
    fun `shelves retain slope range and peaking retains width modes`() {
        val shelf = EqBand(true, FilterType.LowShelf, WidthMode.Q, 100.0, 3.0, 1.0, 1.0)
        assertNull(EqValidation.band(shelf, 0))
        assertNotNull(EqValidation.band(shelf.copy(q = 1.01), 0))
        val peaking = shelf.copy(type = FilterType.Peaking, widthMode = WidthMode.Bandwidth, q = 20.0)
        assertNull(EqValidation.band(peaking, 0))
    }
}
