package dev.lightwolf.pumper.controller.transport

import kotlinx.coroutines.flow.Flow

interface PumperTransport {
    val productName: String
    val reports: Flow<ByteArray>
    val disconnects: Flow<Unit>

    suspend fun sendReport(report: ByteArray)
    suspend fun close()
}

data class PumperDevice(
    val id: Int,
    val productName: String,
    internal val platformDevice: Any,
)

class TransportException(message: String, cause: Throwable? = null) : Exception(message, cause)

