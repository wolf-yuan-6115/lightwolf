package dev.lightwolf.pumper.controller.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class PumperProtocolTest {
    @Test
    fun `audio and crossfeed opcodes match firmware contract`() {
        assertEquals(0x06, Opcode.GetAudioControls.value)
        assertEquals(0x07, Opcode.GetCrossfeed.value)
        assertEquals(0x12, Opcode.SetCrossfeed.value)
        assertEquals(0x26, Opcode.SaveCrossfeed.value)
    }

    @Test
    fun `builds fixed-size little-endian request`() {
        val report = PumperProtocol.createRequest(Opcode.GetBand, 0x1234, byteArrayOf(7))
        assertEquals(REPORT_SIZE, report.size)
        assertArrayEquals(
            byteArrayOf(80, 69, 1, Opcode.GetBand.value.toByte(), 0x34, 0x12, 1, 0, 7),
            report.copyOfRange(0, 9),
        )
    }

    @Test
    fun `round trips global and band milli values`() {
        val config = EqConfig(true, -5.321, emptyList())
        assertEquals(true to -5.321, PumperProtocol.decodeGlobal(PumperProtocol.encodeGlobal(config)))
        val band = EqBand(true, FilterType.Peaking, WidthMode.Bandwidth, 1234.5, -2.75, 1.41, 0.82)
        assertEquals(4 to band, PumperProtocol.decodeBand(PumperProtocol.encodeBand(4, band)))
    }

    @Test
    fun `decodes status variants`() {
        val payload = ByteArray(44)
        val view = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        payload[0] = 1
        payload[1] = 9
        payload[2] = 10
        payload[3] = 0x04
        view.putInt(4, 192000)
        view.putInt(28, 42375)
        view.putInt(32, 180000000)
        view.putInt(36, 417)
        view.putInt(40, 322)

        val current = PumperProtocol.decodeStatus(payload)
        assertEquals("1.9", current.firmwareVersion)
        assertEquals(42.375, current.temperatureC!!, 0.0001)
        assertEquals(180.0, current.systemClockMHz!!, 0.0001)
        assertEquals(417L, current.maxDspBlockUs)
        assertTrue(current.supportsDeviceReset)
        assertFalse(current.supportsAudioControls)
        payload[0] = 2
        payload[1] = 2
        assertTrue(PumperProtocol.decodeStatus(payload).supportsAudioControls)
        assertNull(PumperProtocol.decodeStatus(payload.copyOf(28)).temperatureC)
        assertThrows(ProtocolException::class.java) { PumperProtocol.decodeStatus(ByteArray(30)) }
    }

    @Test
    fun `encodes meter timing and decodes levels`() {
        assertArrayEquals(byteArrayOf(40, 0, 0xe2.toByte(), 0x04), PumperProtocol.encodeMeterConfig(40, 1250))
        val payload = ByteArray(28)
        ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0, 17)
            putShort(4, 32768.toShort())
            putShort(6, 16384.toShort())
            putInt(8, 536870912)
            putInt(12, 134217728)
            putShort(16, 24576.toShort())
            putShort(18, 8192.toShort())
            putInt(20, 301989888)
            putInt(24, 33554432)
        }
        val meter = PumperProtocol.decodeMeterLevel(payload)
        assertEquals(17L, meter.sequence)
        assertEquals(32768, meter.preEq.leftPeak)
        assertEquals(301989888L, meter.postEq.leftMeanSquare)
    }

    @Test
    fun `decodes profile state`() {
        val payload = ByteArray(12)
        val view = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        payload[0] = 10
        payload[1] = 3
        payload[2] = 1
        view.putShort(4, 0x020b)
        view.putInt(8, 27)
        val state = PumperProtocol.decodeProfileState(payload)
        assertEquals(3, state.activeProfile)
        assertEquals(1, state.persistedProfile)
        assertTrue(state.isPresent(9))
        assertFalse(state.isPresent(8))
    }

    @Test
    fun `audio controls use signed little-endian Q8 8 values`() {
        val controls = AudioControls(
            master = AudioChannelControl(-12.5, false),
            left = AudioChannelControl(-3.0, true),
            right = AudioChannelControl(0.0, false),
        )
        val payload = PumperProtocol.encodeAudioControls(controls)
        assertArrayEquals(
            byteArrayOf(0x80.toByte(), 0xf3.toByte(), 0x00, 0xfd.toByte(), 0, 0, 0, 1, 0),
            payload,
        )
        assertEquals(controls, PumperProtocol.decodeAudioControls(payload))
        assertThrows(ProtocolException::class.java) { PumperProtocol.decodeAudioControls(ByteArray(8)) }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.decodeAudioControls(payload.copyOf().also { it[7] = 2 })
        }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.decodeAudioControls(payload.copyOf().also { it[0] = 0; it[1] = 1 })
        }
    }

    @Test
    fun `crossfeed record and dirty state round trip`() {
        val live = CrossfeedConfig(CrossfeedMode.Custom, 21.25, 1_230, 0.37)
        val saved = CrossfeedConfig(CrossfeedMode.Medium, 20.0, 700, 0.25)
        val record = PumperProtocol.encodeCrossfeed(live)
        assertArrayEquals(
            byteArrayOf(4, 0, 0x4d, 0x08, 0xce.toByte(), 0x04, 0x72, 0x01),
            record,
        )
        assertEquals(live, PumperProtocol.decodeCrossfeed(record))

        val state = CrossfeedState(live, saved, true)
        assertEquals(state, PumperProtocol.decodeCrossfeedState(PumperProtocol.encodeCrossfeedState(state)))
    }

    @Test
    fun `crossfeed rejects malformed and out of range records`() {
        val valid = PumperProtocol.encodeCrossfeed(DefaultCrossfeedConfig)
        assertThrows(ProtocolException::class.java) { PumperProtocol.decodeCrossfeed(ByteArray(7)) }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.decodeCrossfeed(valid.copyOf().also { it[1] = 1 })
        }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.decodeCrossfeed(valid.copyOf().also { it[0] = 5 })
        }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.encodeCrossfeed(DefaultCrossfeedConfig.copy(strengthPercent = 40.01))
        }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.encodeCrossfeed(DefaultCrossfeedConfig.copy(cutoffHz = 2_010))
        }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.encodeCrossfeed(DefaultCrossfeedConfig.copy(delayMs = 0.61))
        }
        assertThrows(ProtocolException::class.java) {
            PumperProtocol.decodeCrossfeedState(PumperProtocol.encodeCrossfeedState(
                CrossfeedState(DefaultCrossfeedConfig, DefaultCrossfeedConfig, false),
            ).also { it[16] = 2 })
        }
    }
}
