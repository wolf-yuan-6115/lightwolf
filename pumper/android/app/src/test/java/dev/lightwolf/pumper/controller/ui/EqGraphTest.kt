package dev.lightwolf.pumper.controller.ui

import dev.lightwolf.pumper.controller.protocol.ResponsePoint
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class EqGraphTest {
    @Test
    fun `flat response uses centered twelve decibel span`() {
        val axis = calculateGainAxis(listOf(ResponsePoint(1_000.0, 0.0)))

        assertEquals(-6.0, axis.minimum, 0.0)
        assertEquals(6.0, axis.maximum, 0.0)
        assertEquals(3.0, axis.tickStep, 0.0)
        assertTrue(0.0 in axis.ticks)
    }

    @Test
    fun `axis includes full response and rounds outward`() {
        val axis = calculateGainAxis(
            listOf(ResponsePoint(20.0, -13.2), ResponsePoint(1_000.0, 7.1), ResponsePoint(20_000.0, 2.0)),
        )

        assertTrue(axis.minimum <= -13.2)
        assertTrue(axis.maximum >= 7.1)
        assertEquals(0.0, axis.minimum % 3.0, 0.0)
        assertEquals(0.0, axis.maximum % 3.0, 0.0)
        assertTrue(0.0 in axis.minimum..axis.maximum)
    }
}
