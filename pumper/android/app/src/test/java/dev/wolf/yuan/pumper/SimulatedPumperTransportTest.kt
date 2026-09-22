package dev.wolf.yuan.pumper

import dev.wolf.yuan.pumper.protocol.CrossfeedConfig
import dev.wolf.yuan.pumper.protocol.CrossfeedMode
import dev.wolf.yuan.pumper.protocol.Opcode
import dev.wolf.yuan.pumper.protocol.OutputProcessingConfig
import dev.wolf.yuan.pumper.protocol.PumperProtocol
import dev.wolf.yuan.pumper.transport.PumperClient
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class SimulatedPumperTransportTest {
    @Test
    fun `simulator exposes firmware 3 master audio state`() = runTest {
        val client = PumperClient(SimulatedPumperTransport(), backgroundScope)
        runCurrent()

        val status = PumperProtocol.decodeStatus(client.request(Opcode.Hello).payload)
        val audio = PumperProtocol.decodeAudioControls(client.request(Opcode.GetAudioControls).payload)

        assertEquals("3.3", status.firmwareVersion)
        assertEquals(16, status.bitDepth)
        assertTrue(status.supportsAudioControls)
        assertTrue(status.supportsFirmware3Controls)
        assertEquals(-10.0, audio.master.volumeDb, 0.0)
        assertFalse(audio.master.muted)
        assertEquals(0.0, audio.left.volumeDb, 0.0)
        assertEquals(0.0, audio.right.volumeDb, 0.0)
        client.close()
    }

    @Test
    fun `output preview remains independent and persists only through output save`() = runTest {
        val client = PumperClient(SimulatedPumperTransport(), backgroundScope)
        runCurrent()
        val custom = OutputProcessingConfig(true, true, false, true, -24.0, 135.0)

        val preview = PumperProtocol.decodeOutputProcessingState(
            client.request(Opcode.SetOutputProcessing, PumperProtocol.encodeOutputProcessing(custom)).payload,
        )
        assertEquals(custom, preview.live)
        assertTrue(preview.dirty)

        client.request(Opcode.SaveProfile, byteArrayOf(0))
        val afterEqSave = PumperProtocol.decodeOutputProcessingState(client.request(Opcode.GetOutputProcessing).payload)
        assertEquals(custom, afterEqSave.live)
        assertTrue(afterEqSave.dirty)

        client.request(Opcode.SaveCrossfeed)
        val afterCrossfeedSave = PumperProtocol.decodeOutputProcessingState(client.request(Opcode.GetOutputProcessing).payload)
        assertTrue(afterCrossfeedSave.dirty)

        val saved = PumperProtocol.decodeOutputProcessingState(client.request(Opcode.SaveOutputProcessing).payload)
        assertEquals(custom, saved.saved)
        assertFalse(saved.dirty)
        client.close()
    }

    @Test
    fun `crossfeed preview is live until explicitly saved`() = runTest {
        val client = PumperClient(SimulatedPumperTransport(), backgroundScope)
        runCurrent()
        val custom = CrossfeedConfig(CrossfeedMode.Custom, 27.0, 1_100, 0.42)

        val preview = PumperProtocol.decodeCrossfeedState(
            client.request(Opcode.SetCrossfeed, PumperProtocol.encodeCrossfeed(custom)).payload,
        )
        assertEquals(custom, preview.live)
        assertTrue(preview.dirty)

        client.request(Opcode.SaveProfile, byteArrayOf(0))
        val afterEqSave = PumperProtocol.decodeCrossfeedState(client.request(Opcode.GetCrossfeed).payload)
        assertEquals(custom, afterEqSave.live)
        assertTrue(afterEqSave.dirty)

        val saved = PumperProtocol.decodeCrossfeedState(client.request(Opcode.SaveCrossfeed).payload)
        assertEquals(custom, saved.saved)
        assertFalse(saved.dirty)
        client.close()
    }

    @Test
    fun `preset selection retains custom parameters`() = runTest {
        val client = PumperClient(SimulatedPumperTransport(), backgroundScope)
        runCurrent()
        val custom = CrossfeedConfig(CrossfeedMode.Custom, 31.0, 1_420, 0.51)
        client.request(Opcode.SetCrossfeed, PumperProtocol.encodeCrossfeed(custom))

        val preset = custom.copy(mode = CrossfeedMode.Low)
        val state = PumperProtocol.decodeCrossfeedState(
            client.request(Opcode.SetCrossfeed, PumperProtocol.encodeCrossfeed(preset)).payload,
        )
        assertEquals(31.0, state.live.strengthPercent, 0.0)
        assertEquals(1_420, state.live.cutoffHz)
        assertEquals(0.51, state.live.delayMs, 0.0)
        client.close()
    }
}
