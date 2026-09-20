package dev.lightwolf.pumper.controller

import dev.lightwolf.pumper.controller.protocol.DefaultEqConfig
import dev.lightwolf.pumper.controller.protocol.AudioChannelControl
import dev.lightwolf.pumper.controller.protocol.AudioControls
import dev.lightwolf.pumper.controller.protocol.CrossfeedState
import dev.lightwolf.pumper.controller.protocol.DefaultCrossfeedConfig
import dev.lightwolf.pumper.controller.protocol.DefaultOutputProcessingConfig
import dev.lightwolf.pumper.controller.protocol.EqBand
import dev.lightwolf.pumper.controller.protocol.EqConfig
import dev.lightwolf.pumper.controller.protocol.FilterType
import dev.lightwolf.pumper.controller.protocol.Opcode
import dev.lightwolf.pumper.controller.protocol.OutputProcessingState
import dev.lightwolf.pumper.controller.protocol.PumperProtocol
import dev.lightwolf.pumper.controller.protocol.REPORT_SIZE
import dev.lightwolf.pumper.controller.protocol.WidthMode
import dev.lightwolf.pumper.controller.transport.PumperTransport
import dev.lightwolf.pumper.controller.transport.TransportException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

internal class SimulatedPumperTransport : PumperTransport {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val _reports = MutableSharedFlow<ByteArray>(extraBufferCapacity = 32)
    private val _disconnects = MutableSharedFlow<Unit>(extraBufferCapacity = 1)
    private val storedProfiles = mutableMapOf<Int, EqConfig>()
    private var config = DefaultEqConfig
    private var activeProfile = 0
    private var persistedProfile = 0
    private var configGeneration = 1L
    private var savedGeneration = 1L
    private var bankGeneration = 1L
    private val audioControls = AudioControls(
        master = AudioChannelControl(-10.0, false),
        left = AudioChannelControl(0.0, false),
        right = AudioChannelControl(0.0, false),
    )
    private var liveCrossfeed = DefaultCrossfeedConfig
    private var savedCrossfeed = DefaultCrossfeedConfig
    private var liveOutputProcessing = DefaultOutputProcessingConfig
    private var savedOutputProcessing = DefaultOutputProcessingConfig
    private var meterJob: Job? = null
    private var meterIntervalMs = 20L
    private var meterSequence = 0L
    private var closed = false

    init {
        storedProfiles[0] = DefaultEqConfig
        storedProfiles[1] = DefaultEqConfig.copy(
            preampDb = -4.0,
            bands = DefaultEqConfig.bands.mapIndexed { index, band ->
                when (index) {
                    0 -> band.copy(gainDb = 4.0, frequencyHz = 72.0)
                    6 -> band.copy(gainDb = 2.0)
                    else -> band
                }
            },
        )
    }

    override val productName: String = "Pumper Simulator"
    override val reports: Flow<ByteArray> = _reports.asSharedFlow()
    override val disconnects: Flow<Unit> = _disconnects.asSharedFlow()

    override suspend fun sendReport(report: ByteArray) {
        if (closed) throw TransportException("Simulator is disconnected")
        if (report.size != REPORT_SIZE || report[0] != 'P'.code.toByte() || report[1] != 'E'.code.toByte()) {
            throw TransportException("Malformed simulator request")
        }
        delay(8)
        val opcode = Opcode.entries.firstOrNull { it.value == report.u8(3) }
            ?: throw TransportException("Unsupported simulator command")
        val requestId = report.u16(4)
        val payloadSize = report.u8(6)
        val payload = report.copyOfRange(8, 8 + payloadSize)
        val responsePayload = handle(opcode, payload)
        _reports.emit(response(opcode, requestId, responsePayload))
        if (opcode == Opcode.RestartDevice || opcode == Opcode.EnterBootsel) {
            scope.launch {
                delay(80)
                _disconnects.emit(Unit)
                close()
            }
        }
    }

    override suspend fun close() {
        if (closed) return
        closed = true
        meterJob?.cancel()
        scope.cancel()
    }

    private fun handle(opcode: Opcode, payload: ByteArray): ByteArray = when (opcode) {
        Opcode.Hello, Opcode.GetStatus -> encodeStatus()
        Opcode.GetGlobal -> PumperProtocol.encodeGlobal(config)
        Opcode.GetBand -> PumperProtocol.encodeBand(payload.u8(0), config.bands[payload.u8(0)])
        Opcode.GetProfiles -> encodeProfiles()
        Opcode.GetAudioControls -> PumperProtocol.encodeAudioControls(audioControls)
        Opcode.GetCrossfeed -> encodeCrossfeedState()
        Opcode.GetOutputProcessing -> encodeOutputProcessingState()
        Opcode.SetGlobal -> {
            val (enabled, preampDb) = PumperProtocol.decodeGlobal(payload)
            config = config.copy(enabled = enabled, preampDb = preampDb)
            configGeneration++
            byteArrayOf()
        }
        Opcode.SetBand -> {
            val (index, band) = PumperProtocol.decodeBand(payload)
            config = config.copy(bands = config.bands.toMutableList().also { it[index] = band })
            configGeneration++
            byteArrayOf()
        }
        Opcode.SetCrossfeed -> {
            liveCrossfeed = PumperProtocol.decodeCrossfeed(payload)
            encodeCrossfeedState()
        }
        Opcode.SetOutputProcessing -> {
            liveOutputProcessing = PumperProtocol.decodeOutputProcessing(payload)
            encodeOutputProcessingState()
        }
        Opcode.LoadProfile -> {
            val index = payload.u8(0)
            config = storedProfiles[index] ?: throw TransportException("Profile ${index + 1} is empty")
            activeProfile = index
            configGeneration++
            savedGeneration = configGeneration
            byteArrayOf()
        }
        Opcode.SaveProfile -> {
            val index = payload.u8(0)
            storedProfiles[index] = config
            activeProfile = index
            savedGeneration = configGeneration
            bankGeneration++
            encodeProfiles()
        }
        Opcode.SetDefaultProfile -> {
            persistedProfile = payload.u8(0)
            bankGeneration++
            encodeProfiles()
        }
        Opcode.DeleteProfile -> {
            storedProfiles.remove(payload.u8(0))
            bankGeneration++
            encodeProfiles()
        }
        Opcode.SaveCrossfeed -> {
            savedCrossfeed = liveCrossfeed
            bankGeneration++
            encodeCrossfeedState()
        }
        Opcode.SaveOutputProcessing -> {
            savedOutputProcessing = liveOutputProcessing
            bankGeneration++
            encodeOutputProcessingState()
        }
        Opcode.RestoreDefaults -> {
            config = DefaultEqConfig
            configGeneration++
            byteArrayOf()
        }
        Opcode.MeterStart -> {
            meterIntervalMs = payload.u16(0).toLong().coerceIn(20L, 250L)
            startMetering()
            byteArrayOf()
        }
        Opcode.MeterKeepalive -> byteArrayOf()
        Opcode.MeterStop -> {
            meterJob?.cancel()
            meterJob = null
            byteArrayOf()
        }
        Opcode.RestartDevice, Opcode.EnterBootsel -> byteArrayOf()
        Opcode.WriteFlash, Opcode.MeterLevel -> byteArrayOf()
    }

    private fun startMetering() {
        meterJob?.cancel()
        meterJob = scope.launch {
            while (isActive) {
                delay(meterIntervalMs)
                meterSequence++
                _reports.emit(response(Opcode.MeterLevel, 0, encodeMeter()))
            }
        }
    }

    private fun encodeStatus(): ByteArray = ByteArray(48).also { payload ->
        payload[0] = 3
        payload[1] = 1
        payload[2] = config.bands.size.toByte()
        var flags = 0x01
        if (config != storedProfiles[activeProfile]) flags = flags or 0x02
        if (config.enabled) flags = flags or 0x04
        payload[3] = flags.toByte()
        payload.putU32(4, 48_000)
        payload.putU32(8, configGeneration)
        payload.putU32(12, savedGeneration)
        payload.putU32(16, configGeneration)
        payload.putU32(20, 0)
        payload.putU32(24, 0)
        payload.putI32(28, 41_750)
        payload.putU32(32, 180_000_000)
        payload.putU32(36, 386)
        payload.putU32(40, 348)
        payload[44] = 16
    }

    private fun encodeProfiles(): ByteArray = ByteArray(12).also { payload ->
        payload[0] = 10
        payload[1] = activeProfile.toByte()
        payload[2] = persistedProfile.toByte()
        val mask = storedProfiles.keys.fold(0) { value, index -> value or (1 shl index) }
        payload.putU16(4, mask)
        payload.putU32(8, bankGeneration)
    }

    private fun encodeCrossfeedState(): ByteArray = PumperProtocol.encodeCrossfeedState(
        CrossfeedState(
            live = liveCrossfeed,
            saved = savedCrossfeed,
            dirty = liveCrossfeed != savedCrossfeed,
        ),
    )

    private fun encodeOutputProcessingState(): ByteArray = PumperProtocol.encodeOutputProcessingState(
        OutputProcessingState(
            live = liveOutputProcessing,
            saved = savedOutputProcessing,
            dirty = liveOutputProcessing != savedOutputProcessing,
        ),
    )

    private fun encodeMeter(): ByteArray = ByteArray(28).also { payload ->
        val phase = meterSequence / 18.0
        val left = (11_000 + 17_000 * abs(sin(phase))).toInt()
        val right = (9_000 + 15_000 * abs(sin(phase + PI / 3))).toInt()
        val postScale = if (config.enabled) 0.82 else 1.0
        val postLeft = (left * postScale).toInt()
        val postRight = (right * postScale).toInt()
        payload.putU32(0, meterSequence)
        payload.putU16(4, left)
        payload.putU16(6, right)
        payload.putU32(8, left.toLong() * left / 2)
        payload.putU32(12, right.toLong() * right / 2)
        payload.putU16(16, postLeft)
        payload.putU16(18, postRight)
        payload.putU32(20, postLeft.toLong() * postLeft / 2)
        payload.putU32(24, postRight.toLong() * postRight / 2)
    }

    private fun response(opcode: Opcode, requestId: Int, payload: ByteArray): ByteArray = ByteArray(REPORT_SIZE).also {
        it[0] = 'P'.code.toByte()
        it[1] = 'E'.code.toByte()
        it[2] = 1
        it[3] = (opcode.value or 0x80).toByte()
        it.putU16(4, requestId)
        it[6] = payload.size.toByte()
        it[7] = 0
        payload.copyInto(it, 8)
    }
}

private fun ByteArray.buffer(): ByteBuffer = ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN)
private fun ByteArray.u8(offset: Int): Int = this[offset].toInt() and 0xff
private fun ByteArray.u16(offset: Int): Int = buffer().getShort(offset).toInt() and 0xffff
private fun ByteArray.putU16(offset: Int, value: Int) { buffer().putShort(offset, value.toShort()) }
private fun ByteArray.putU32(offset: Int, value: Long) { buffer().putInt(offset, value.toInt()) }
private fun ByteArray.putI32(offset: Int, value: Int) { buffer().putInt(offset, value) }
