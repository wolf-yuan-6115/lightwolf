package dev.wolf.yuan.pumper.transport

import dev.wolf.yuan.pumper.protocol.SpectrumAssembler
import dev.wolf.yuan.pumper.protocol.SpectrumFrame
import dev.wolf.yuan.pumper.protocol.decodeSpectrumChunk
import dev.wolf.yuan.pumper.protocol.ProtocolStatus
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import dev.wolf.yuan.pumper.protocol.MeterLevel
import dev.wolf.yuan.pumper.protocol.Opcode
import dev.wolf.yuan.pumper.protocol.PumperProtocol
import dev.wolf.yuan.pumper.protocol.ResponsePacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout

class PumperClient(
    private val transport: PumperTransport,
    scope: CoroutineScope,
) {
    private val requestMutex = Mutex()
    private val responses = Channel<ResponsePacket>(Channel.UNLIMITED)
    private val _meterLevels = MutableSharedFlow<MeterLevel>(
        extraBufferCapacity = 1,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    private val _spectrum = MutableStateFlow<SpectrumFrame?>(null)
    val spectrum = _spectrum.asStateFlow()
    private var spectrumAssembler = SpectrumAssembler()
    private var spectrumEpoch = 0L
    private var spectrumActive = false
    private var requestId = 0
    private val readerJob: Job = scope.launch {
        transport.reports.collect { report ->
            runCatching { PumperProtocol.parseResponse(report) }.getOrNull()?.let { response ->
                if (response.requestId == 0 && response.opcode == (Opcode.MeterLevel.value or 0x80)) {
                    val meter = runCatching { PumperProtocol.decodeMeterLevel(response.payload) }.getOrNull()
                    if (meter != null) _meterLevels.tryEmit(meter)
                } else if (response.requestId == 0 && response.opcode == (Opcode.SpectrumData.value or 0x80) && spectrumActive && response.status == ProtocolStatus.Ok) {
                    val now = System.nanoTime() / 1_000_000
                    runCatching { decodeSpectrumChunk(response.payload) }.getOrNull()?.let { chunk ->
                        spectrumAssembler.push(chunk, now)?.let { levels ->
                            _spectrum.value = SpectrumFrame(levels, now, spectrumEpoch)
                        }
                    }
                } else if (response.requestId != 0) {
                    // Reset at the acknowledged report boundary, before subsequent data is read.
                    if (response.requestId == requestId && response.status == ProtocolStatus.Ok &&
                        response.opcode == (Opcode.SpectrumStart.value or 0x80)) {
                        spectrumAssembler = SpectrumAssembler()
                        spectrumEpoch++
                        spectrumActive = true
                        _spectrum.value = null
                    }
                    if (response.opcode == (Opcode.SpectrumStop.value or 0x80)) {
                        spectrumActive = false
                        _spectrum.value = null
                    }
                    responses.send(response)
                }
            }
        }
    }

    val productName: String get() = transport.productName
    val meterLevels: Flow<MeterLevel> = _meterLevels.asSharedFlow()
    val disconnects: Flow<Unit> get() = transport.disconnects

    suspend fun request(opcode: Opcode, payload: ByteArray = byteArrayOf(), timeoutMs: Long = 1_500): ResponsePacket =
        requestMutex.withLock {
            requestId = (requestId + 1) and 0xffff
            if (requestId == 0) requestId = 1
            val expectedId = requestId
            transport.sendReport(PumperProtocol.createRequest(opcode, expectedId, payload))
            val response = withTimeout(timeoutMs) {
                while (true) {
                    val candidate = responses.receive()
                    if (candidate.requestId == expectedId) return@withTimeout candidate
                }
                error("unreachable")
            }
            PumperProtocol.assertResponse(response, opcode)
            response
        }

    suspend fun close() {
        spectrumActive = false
        _spectrum.value = null
        readerJob.cancel()
        responses.close()
        transport.close()
    }
}
