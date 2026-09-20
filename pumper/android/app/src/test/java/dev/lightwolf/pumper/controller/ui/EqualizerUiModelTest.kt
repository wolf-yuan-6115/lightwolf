package dev.lightwolf.pumper.controller.ui

import dev.lightwolf.pumper.controller.protocol.FilterType
import org.junit.Assert.assertEquals
import org.junit.Test

class EqualizerUiModelTest {
    @Test
    fun `firmware 3 filter order matches the editor`() {
        assertEquals(
            listOf(
                FilterType.Peaking,
                FilterType.LowPass,
                FilterType.LowShelf,
                FilterType.HighPass,
                FilterType.HighShelf,
                FilterType.Notch,
                FilterType.BandPass,
            ),
            filterOptionsForFirmware(true),
        )
        assertEquals(
            listOf(FilterType.Peaking, FilterType.LowShelf, FilterType.HighShelf),
            filterOptionsForFirmware(false),
        )
    }
}
