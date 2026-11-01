package dev.wolf.yuan.pumper.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

class SpectrumTest {
    private fun payload(index: Int, sequence: Long = 1, value: Int = 120): ByteArray = ByteArray(if (index == 5) 48 else 56).also {
        it[0] = 1; it[1] = index.toByte(); it[2] = 6; it[3] = (it.size - 12).toByte()
        ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN).putInt(4, sequence.toInt()).putInt(8, 48000)
        it.fill(value.toByte(), 12)
    }
    private fun chunk(index: Int, sequence: Long = 1, value: Int = 120) = decodeSpectrumChunk(payload(index, sequence, value))
    @Test fun supportsSpectrumFromFirmware35() {
        fun supported(major: Int, minor: Int) = PumperProtocol.decodeStatus(ByteArray(48).also {
            it[0] = major.toByte(); it[1] = minor.toByte(); it[2] = 10; it[44] = 16
        }).supportsSpectrum
        assertFalse(supported(3, 4))
        assertTrue(supported(3, 5))
        assertTrue(supported(4, 0))
    }
    @Test fun validatesWireData() {
        assertEquals(36, chunk(5).levels.size)
        listOf(byteArrayOf(), payload(6), payload(0, value = 193), payload(0).also { it[0] = 2 }, payload(0).also { it[8] = 0 }).forEach {
            assertTrue(runCatching { decodeSpectrumChunk(it) }.isFailure)
        }
    }
    @Test fun assemblesOutOfOrderAndRejectsReplay() {
        val assembler = SpectrumAssembler()
        listOf(5, 2, 0, 4, 1).forEach { assertNull(assembler.push(chunk(it), 0)) }
        assertEquals(List(256) { 120 }, assembler.push(chunk(3), 1))
        repeat(6) { assertNull(assembler.push(chunk(it), 2)) }
        repeat(6) { assertNull(assembler.push(chunk(it, 0), 2)) }
    }
    @Test fun rejectsConflictingDuplicatesExpiredFramesAndMixedRates() {
        val assembler = SpectrumAssembler()
        assembler.push(chunk(0), 0)
        assembler.push(chunk(0, value = 121), 0)
        (1..5).forEach { assertNull(assembler.push(chunk(it), 0)) }
        assembler.push(chunk(0, 2), 0)
        (1..5).forEach { assertNull(assembler.push(chunk(it, 2), 251)) }
        assembler.push(chunk(0, 3), 300)
        assertNull(assembler.push(chunk(1, 3).copy(rate = 44100), 300))
        (2..5).forEach { assertNull(assembler.push(chunk(it, 3), 300)) }
    }
    @Test fun sequenceWrapAndNewSubscription() {
        val assembler = SpectrumAssembler()
        repeat(6) { assembler.push(chunk(it, 0xffffffffL), 0) }
        var result: List<Int>? = null
        repeat(6) { result = assembler.push(chunk(it, 0), 10) }
        assertNotNull(result)
        val fresh = SpectrumAssembler()
        repeat(6) { result = fresh.push(chunk(it, 0), 0) }
        assertNotNull(result)
    }
    @Test fun smoothingUsesDbHeightAndAsymmetricTiming() {
        val rise = smoothSpectrum(FloatArray(256) { -96f }, List(256) { 192 }, 8.0)
        val fall = smoothSpectrum(FloatArray(256), List(256) { 0 }, 70.0)
        assertEquals(-35.32f, rise[0], 0.1f)
        assertEquals(-60.68f, fall[0], 0.1f)
        assertEquals(-96f, smoothSpectrum(FloatArray(256) { -96f }, List(256) { 192 }, 0.0)[0], 0f)
    }
}
