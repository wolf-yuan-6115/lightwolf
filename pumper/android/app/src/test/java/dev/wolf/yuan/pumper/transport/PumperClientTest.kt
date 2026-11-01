package dev.wolf.yuan.pumper.transport

import dev.wolf.yuan.pumper.protocol.Opcode
import dev.wolf.yuan.pumper.protocol.REPORT_SIZE
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

@OptIn(ExperimentalCoroutinesApi::class)
class PumperClientTest {
    @Test
    fun `matches command responses by request id`() = runTest {
        val transport = FakeTransport()
        val client = PumperClient(transport, backgroundScope)
        val response = client.request(Opcode.GetGlobal)

        assertEquals(Opcode.GetGlobal.value or 0x80, response.opcode)
        assertTrue(response.requestId > 0)
        assertEquals(8, response.payload.size)
        client.close()
    }

    @Test
    fun `routes unsolicited meter reports away from command responses`() = runTest {
        val transport = FakeTransport()
        val client = PumperClient(transport, backgroundScope)
        val meter = async { client.meterLevels.firstValue() }
        advanceUntilIdle()

        transport.emitMeter(sequence = 42)
        assertEquals(42L, meter.await().sequence)
        client.close()
    }

    @Test
    fun `spectrum frames reset only at acknowledged subscription boundaries`() = runTest {
        val transport = FakeTransport()
        val client = PumperClient(transport, backgroundScope)
        runCurrent()
        transport.emitSpectrum(9)
        runCurrent()
        assertEquals(null, client.spectrum.value)
        client.request(Opcode.SpectrumStart)
        transport.emitSpectrum(9)
        runCurrent()
        val first = requireNotNull(client.spectrum.value)
        assertEquals(256, first.levels.size)
        transport.emitSpectrum(8)
        runCurrent()
        assertEquals(first, client.spectrum.value)
        client.request(Opcode.SpectrumStart)
        assertEquals(null, client.spectrum.value)
        transport.emitSpectrum(0)
        runCurrent()
        assertTrue(requireNotNull(client.spectrum.value).epoch > first.epoch)
        client.request(Opcode.SpectrumStop)
        transport.emitSpectrum(1)
        runCurrent()
        assertEquals(null, client.spectrum.value)
        client.close()
    }

    @Test
    fun `slow meter consumer receives latest report without a backlog`() = runTest {
        val transport = FakeTransport()
        val client = PumperClient(transport, backgroundScope)
        val sequences = mutableListOf<Long>()
        val collector = backgroundScope.launch {
            client.meterLevels.collect { meter ->
                sequences += meter.sequence
                delay(1_000)
            }
        }
        runCurrent()

        repeat(20) { index ->
            transport.emitMeter(sequence = index + 1)
            runCurrent()
        }
        advanceTimeBy(1_000)
        runCurrent()

        assertEquals(20L, sequences.last())
        assertTrue(sequences.size < 20)
        collector.cancel()
        client.close()
    }
}

private class FakeTransport : PumperTransport {
    private val _reports = MutableSharedFlow<ByteArray>(replay = 1, extraBufferCapacity = 4)
    private val _disconnects = MutableSharedFlow<Unit>()
    override val productName: String = "Test Pumper"
    override val reports: Flow<ByteArray> = _reports.asSharedFlow()
    override val disconnects: Flow<Unit> = _disconnects.asSharedFlow()

    override suspend fun sendReport(report: ByteArray) {
        val opcode = report[3].toInt() and 0xff
        val requestId = report.u16(4)
        val payload = if (opcode == Opcode.GetGlobal.value) ByteArray(8) else byteArrayOf()
        _reports.emit(response(opcode, requestId, payload))
    }

    suspend fun emitMeter(sequence: Int) {
        val payload = ByteArray(28)
        ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN).putInt(0, sequence)
        _reports.emit(response(Opcode.MeterLevel.value, 0, payload))
    }

    suspend fun emitSpectrum(sequence: Int) {
        repeat(6) { index ->
            val count = if (index == 5) 36 else 44
            val payload = ByteArray(12 + count)
            payload[0] = 1; payload[1] = index.toByte(); payload[2] = 6; payload[3] = count.toByte()
            ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN).putInt(4, sequence).putInt(8, 48000)
            payload.fill(120, 12)
            _reports.emit(response(Opcode.SpectrumData.value, 0, payload))
        }
    }

    override suspend fun close() = Unit

    private fun response(opcode: Int, requestId: Int, payload: ByteArray): ByteArray = ByteArray(REPORT_SIZE).also {
        it[0] = 'P'.code.toByte()
        it[1] = 'E'.code.toByte()
        it[2] = 1
        it[3] = (opcode or 0x80).toByte()
        ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN).putShort(4, requestId.toShort())
        it[6] = payload.size.toByte()
        payload.copyInto(it, 8)
    }
}

private fun ByteArray.u16(offset: Int): Int =
    ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).getShort(offset).toInt() and 0xffff

private suspend fun <T> Flow<T>.firstValue(): T = first()
