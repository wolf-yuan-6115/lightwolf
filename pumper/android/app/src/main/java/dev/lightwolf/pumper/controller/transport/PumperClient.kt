package dev.lightwolf.pumper.controller.transport

import dev.lightwolf.pumper.controller.protocol.MeterLevel
import dev.lightwolf.pumper.controller.protocol.Opcode
import dev.lightwolf.pumper.controller.protocol.PumperProtocol
import dev.lightwolf.pumper.controller.protocol.ResponsePacket
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
    private var requestId = 0
    private val readerJob: Job = scope.launch {
        transport.reports.collect { report ->
            runCatching { PumperProtocol.parseResponse(report) }.getOrNull()?.let { response ->
                if (response.requestId == 0 && response.opcode == (Opcode.MeterLevel.value or 0x80)) {
                    val meter = runCatching { PumperProtocol.decodeMeterLevel(response.payload) }.getOrNull()
                    if (meter != null) _meterLevels.tryEmit(meter)
                } else if (response.requestId != 0) {
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
        readerJob.cancel()
        responses.close()
        transport.close()
    }
}
