package dev.wolf.yuan.pumper.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

const val USB_VENDOR_ID = 0x2e8a
const val USB_PRODUCT_ID = 0xf10a
const val REPORT_SIZE = 64
const val HEADER_SIZE = 8
const val PROTOCOL_VERSION = 1
const val METER_REPORT_INTERVAL_MS = 20
const val METER_HEARTBEAT_INTERVAL_MS = 500L
const val METER_TIMEOUT_MS = 1_250

enum class Opcode(val value: Int) {
    Hello(0x01),
    GetStatus(0x02),
    GetGlobal(0x03),
    GetBand(0x04),
    GetProfiles(0x05),
    GetAudioControls(0x06),
    GetCrossfeed(0x07),
    GetOutputProcessing(0x08),
    SetGlobal(0x10),
    SetBand(0x11),
    SetCrossfeed(0x12),
    SetOutputProcessing(0x13),
    WriteFlash(0x20),
    RestoreDefaults(0x21),
    LoadProfile(0x22),
    SaveProfile(0x23),
    SetDefaultProfile(0x24),
    DeleteProfile(0x25),
    SaveCrossfeed(0x26),
    SaveOutputProcessing(0x27),
    MeterStart(0x30),
    MeterKeepalive(0x31),
    MeterStop(0x32),
    MeterLevel(0x33),
    RestartDevice(0x40),
    EnterBootsel(0x41),
}

enum class ProtocolStatus(val value: Int) {
    Ok(0),
    InvalidPacket(1),
    InvalidCommand(2),
    InvalidLength(3),
    InvalidIndex(4),
    OutOfRange(5),
    Busy(6),
    StorageError(7);

    companion object {
        fun from(value: Int): ProtocolStatus? = entries.firstOrNull { it.value == value }
    }
}

enum class FilterType(val value: Int) {
    LowShelf(0),
    Peaking(1),
    HighShelf(2),
    LowPass(3),
    HighPass(4),
    Notch(5),
    BandPass(6);

    companion object {
        fun from(value: Int): FilterType = entries.firstOrNull { it.value == value }
            ?: throw ProtocolException("Unsupported filter type")
    }
}

enum class WidthMode(val value: Int) {
    Q(0),
    Bandwidth(1);

    companion object {
        fun from(value: Int): WidthMode = entries.firstOrNull { it.value == value }
            ?: throw ProtocolException("Unsupported width mode")
    }
}

data class EqBand(
    val enabled: Boolean,
    val type: FilterType,
    val widthMode: WidthMode,
    val frequencyHz: Double,
    val gainDb: Double,
    val q: Double,
    val bandwidthOctaves: Double,
)

data class EqConfig(
    val enabled: Boolean,
    val preampDb: Double,
    val bands: List<EqBand>,
)

data class DeviceStatus(
    val firmwareMajor: Int,
    val firmwareMinor: Int,
    val bandCount: Int,
    val streaming: Boolean,
    val dirty: Boolean,
    val eqEnabled: Boolean,
    val sampleRateHz: Long,
    val bitDepth: Int?,
    val configGeneration: Long,
    val savedGeneration: Long,
    val appliedGeneration: Long,
    val underrunFrames: Long,
    val backpressureEvents: Long,
    val temperatureC: Double?,
    val systemClockMHz: Double?,
    val maxDspBlockUs: Long?,
    val i2sLowWaterFrames: Long?,
) {
    val firmwareVersion: String get() = "$firmwareMajor.$firmwareMinor"
    val supportsDeviceReset: Boolean get() = firmwareMajor > 1 || (firmwareMajor == 1 && firmwareMinor >= 7)
    val supportsAudioControls: Boolean get() = firmwareMajor > 2 || (firmwareMajor == 2 && firmwareMinor >= 2)
    val supportsFirmware3Controls: Boolean get() = firmwareMajor >= 3
}

data class AudioChannelControl(
    val volumeDb: Double,
    val muted: Boolean,
)

data class AudioControls(
    val master: AudioChannelControl,
    val left: AudioChannelControl,
    val right: AudioChannelControl,
)

enum class CrossfeedMode(val value: Int) {
    Off(0),
    Low(1),
    Medium(2),
    High(3),
    Custom(4);

    companion object {
        fun from(value: Int): CrossfeedMode = entries.firstOrNull { it.value == value }
            ?: throw ProtocolException("Unsupported crossfeed mode")
    }
}

data class CrossfeedConfig(
    val mode: CrossfeedMode,
    val strengthPercent: Double,
    val cutoffHz: Int,
    val delayMs: Double,
)

data class CrossfeedState(
    val live: CrossfeedConfig,
    val saved: CrossfeedConfig,
    val dirty: Boolean,
)

data class OutputProcessingConfig(
    val mono: Boolean,
    val swap: Boolean,
    val invertLeft: Boolean,
    val invertRight: Boolean,
    val balancePercent: Double,
    val widthPercent: Double,
)

data class OutputProcessingState(
    val live: OutputProcessingConfig,
    val saved: OutputProcessingConfig,
    val dirty: Boolean,
)

data class StereoMeterLevel(
    val leftPeak: Int,
    val rightPeak: Int,
    val leftMeanSquare: Long,
    val rightMeanSquare: Long,
)

data class MeterLevel(
    val sequence: Long,
    val preEq: StereoMeterLevel,
    val postEq: StereoMeterLevel,
)

data class ProfileState(
    val count: Int,
    val activeProfile: Int,
    val persistedProfile: Int,
    val presentMask: Int,
    val bankGeneration: Long,
) {
    fun isPresent(index: Int): Boolean = presentMask and (1 shl index) != 0
}

data class ResponsePacket(
    val opcode: Int,
    val requestId: Int,
    val status: ProtocolStatus?,
    val rawStatus: Int,
    val payload: ByteArray,
)

class ProtocolException(message: String) : Exception(message)

object PumperProtocol {
    fun createRequest(opcode: Opcode, requestId: Int, payload: ByteArray = byteArrayOf()): ByteArray {
        require(payload.size <= REPORT_SIZE - HEADER_SIZE) { "Payload is too large" }
        val report = ByteArray(REPORT_SIZE)
        report[0] = 'P'.code.toByte()
        report[1] = 'E'.code.toByte()
        report[2] = PROTOCOL_VERSION.toByte()
        report[3] = opcode.value.toByte()
        report.putU16(4, requestId)
        report[6] = payload.size.toByte()
        payload.copyInto(report, HEADER_SIZE)
        return report
    }

    @Throws(ProtocolException::class)
    fun parseResponse(report: ByteArray): ResponsePacket {
        if (
            report.size != REPORT_SIZE ||
            report[0].toInt() != 'P'.code ||
            report[1].toInt() != 'E'.code ||
            report.u8(2) != PROTOCOL_VERSION ||
            report.u8(6) > REPORT_SIZE - HEADER_SIZE
        ) {
            throw ProtocolException("Malformed response from Pumper")
        }
        val rawStatus = report.u8(7)
        val payloadLength = report.u8(6)
        return ResponsePacket(
            opcode = report.u8(3),
            requestId = report.u16(4),
            status = ProtocolStatus.from(rawStatus),
            rawStatus = rawStatus,
            payload = report.copyOfRange(HEADER_SIZE, HEADER_SIZE + payloadLength),
        )
    }

    @Throws(ProtocolException::class)
    fun assertResponse(response: ResponsePacket, opcode: Opcode) {
        if (response.opcode != (opcode.value or 0x80)) throw ProtocolException("Unexpected response from Pumper")
        if (response.status != ProtocolStatus.Ok) throw ProtocolException(statusLabel(response))
    }

    fun encodeGlobal(config: EqConfig): ByteArray = ByteArray(8).also { payload ->
        payload[0] = if (config.enabled) 1 else 0
        payload.putI32(4, milli(config.preampDb))
    }

    @Throws(ProtocolException::class)
    fun decodeGlobal(payload: ByteArray): Pair<Boolean, Double> {
        if (payload.size != 8) throw ProtocolException("Invalid global EQ response")
        return (payload[0].toInt() != 0) to payload.i32(4) / 1000.0
    }

    fun encodeBand(index: Int, band: EqBand): ByteArray = ByteArray(21).also { payload ->
        payload[0] = index.toByte()
        payload[1] = if (band.enabled) 1 else 0
        payload[2] = band.type.value.toByte()
        payload[3] = band.widthMode.value.toByte()
        payload.putI32(5, milli(band.frequencyHz))
        payload.putI32(9, milli(band.gainDb))
        payload.putI32(13, milli(band.q))
        payload.putI32(17, milli(band.bandwidthOctaves))
    }

    @Throws(ProtocolException::class)
    fun decodeBand(payload: ByteArray): Pair<Int, EqBand> {
        if (payload.size != 21) throw ProtocolException("Invalid EQ band response")
        return payload.u8(0) to EqBand(
            enabled = payload[1].toInt() != 0,
            type = FilterType.from(payload.u8(2)),
            widthMode = WidthMode.from(payload.u8(3)),
            frequencyHz = payload.i32(5) / 1000.0,
            gainDb = payload.i32(9) / 1000.0,
            q = payload.i32(13) / 1000.0,
            bandwidthOctaves = payload.i32(17) / 1000.0,
        )
    }

    @Throws(ProtocolException::class)
    fun decodeStatus(payload: ByteArray): DeviceStatus {
        if (payload.size != 28 && payload.size != 32 && payload.size != 44 && payload.size != 48) {
            throw ProtocolException("Invalid status response")
        }
        val flags = payload.u8(3)
        val bitDepth = if (payload.size >= 48) payload.u8(44) else null
        if (bitDepth != null && bitDepth != 16 && bitDepth != 24) {
            throw ProtocolException("Invalid status bit depth")
        }
        return DeviceStatus(
            firmwareMajor = payload.u8(0),
            firmwareMinor = payload.u8(1),
            bandCount = payload.u8(2),
            streaming = flags and 0x01 != 0,
            dirty = flags and 0x02 != 0,
            eqEnabled = flags and 0x04 != 0,
            sampleRateHz = payload.u32(4),
            bitDepth = bitDepth,
            configGeneration = payload.u32(8),
            savedGeneration = payload.u32(12),
            appliedGeneration = payload.u32(16),
            underrunFrames = payload.u32(20),
            backpressureEvents = payload.u32(24),
            temperatureC = if (payload.size >= 32) payload.i32(28) / 1000.0 else null,
            systemClockMHz = if (payload.size >= 44) payload.u32(32) / 1_000_000.0 else null,
            maxDspBlockUs = if (payload.size >= 44) payload.u32(36) else null,
            i2sLowWaterFrames = if (payload.size >= 44) payload.u32(40) else null,
        )
    }

    fun encodeMeterConfig(reportIntervalMs: Int, timeoutMs: Int): ByteArray = ByteArray(4).also { payload ->
        payload.putU16(0, reportIntervalMs)
        payload.putU16(2, timeoutMs)
    }

    @Throws(ProtocolException::class)
    fun decodeMeterLevel(payload: ByteArray): MeterLevel {
        if (payload.size != 28) throw ProtocolException("Invalid audio meter report")
        return MeterLevel(
            sequence = payload.u32(0),
            preEq = StereoMeterLevel(payload.u16(4), payload.u16(6), payload.u32(8), payload.u32(12)),
            postEq = StereoMeterLevel(payload.u16(16), payload.u16(18), payload.u32(20), payload.u32(24)),
        )
    }

    @Throws(ProtocolException::class)
    fun decodeProfileState(payload: ByteArray): ProfileState {
        if (payload.size != 12) throw ProtocolException("Invalid profile state response")
        return ProfileState(
            count = payload.u8(0),
            activeProfile = payload.u8(1),
            persistedProfile = payload.u8(2),
            presentMask = payload.u16(4),
            bankGeneration = payload.u32(8),
        )
    }

    @Throws(ProtocolException::class)
    fun decodeAudioControls(payload: ByteArray): AudioControls {
        if (payload.size != 9) throw ProtocolException("Invalid USB audio response")
        val volumes = List(3) { index -> payload.i16(index * 2) / 256.0 }
        val mutes = List(3) { index ->
            val value = payload.u8(6 + index)
            if (value !in 0..1) throw ProtocolException("Invalid USB audio mute value")
            value == 1
        }
        if (volumes.any { it !in -50.0..0.0 }) throw ProtocolException("Invalid USB audio volume")
        return AudioControls(
            master = AudioChannelControl(volumes[0], mutes[0]),
            left = AudioChannelControl(volumes[1], mutes[1]),
            right = AudioChannelControl(volumes[2], mutes[2]),
        )
    }

    fun encodeAudioControls(controls: AudioControls): ByteArray {
        val channels = listOf(controls.master, controls.left, controls.right)
        if (channels.any { it.volumeDb !in -50.0..0.0 }) throw ProtocolException("Invalid USB audio volume")
        return ByteArray(9).also { payload ->
            channels.forEachIndexed { index, channel ->
                payload.putI16(index * 2, (channel.volumeDb * 256.0).roundToInt())
                payload[6 + index] = if (channel.muted) 1 else 0
            }
        }
    }

    fun encodeCrossfeed(config: CrossfeedConfig): ByteArray {
        validateCrossfeed(config)
        return ByteArray(8).also { payload ->
            payload[0] = config.mode.value.toByte()
            payload.putU16(2, (config.strengthPercent * 100.0).roundToInt())
            payload.putU16(4, config.cutoffHz)
            payload.putU16(6, (config.delayMs * 1000.0).roundToInt())
        }
    }

    @Throws(ProtocolException::class)
    fun decodeCrossfeed(payload: ByteArray): CrossfeedConfig {
        if (payload.size != 8) throw ProtocolException("Invalid crossfeed response")
        if (payload.u8(1) != 0) throw ProtocolException("Invalid crossfeed reserved field")
        return CrossfeedConfig(
            mode = CrossfeedMode.from(payload.u8(0)),
            strengthPercent = payload.u16(2) / 100.0,
            cutoffHz = payload.u16(4),
            delayMs = payload.u16(6) / 1000.0,
        ).also(::validateCrossfeed)
    }

    @Throws(ProtocolException::class)
    fun decodeCrossfeedState(payload: ByteArray): CrossfeedState {
        if (payload.size != 17) throw ProtocolException("Invalid crossfeed state response")
        val dirty = payload.u8(16)
        if (dirty !in 0..1) throw ProtocolException("Invalid crossfeed dirty state")
        return CrossfeedState(
            live = decodeCrossfeed(payload.copyOfRange(0, 8)),
            saved = decodeCrossfeed(payload.copyOfRange(8, 16)),
            dirty = dirty == 1,
        )
    }

    fun encodeCrossfeedState(state: CrossfeedState): ByteArray = ByteArray(17).also { payload ->
        encodeCrossfeed(state.live).copyInto(payload, 0)
        encodeCrossfeed(state.saved).copyInto(payload, 8)
        payload[16] = if (state.dirty) 1 else 0
    }

    fun encodeOutputProcessing(config: OutputProcessingConfig): ByteArray {
        validateOutputProcessing(config)
        return ByteArray(8).also { payload ->
            var flags = 0
            if (config.mono) flags = flags or 0x01
            if (config.swap) flags = flags or 0x02
            if (config.invertLeft) flags = flags or 0x04
            if (config.invertRight) flags = flags or 0x08
            payload[0] = flags.toByte()
            payload.putI16(2, (config.balancePercent * 100.0).roundToInt())
            payload.putU16(4, (config.widthPercent * 100.0).roundToInt())
        }
    }

    @Throws(ProtocolException::class)
    fun decodeOutputProcessing(payload: ByteArray): OutputProcessingConfig {
        if (payload.size != 8 || payload.u8(1) != 0 || payload.u8(6) != 0 || payload.u8(7) != 0 ||
            payload.u8(0) and 0xf0 != 0
        ) throw ProtocolException("Invalid output processing response")
        val flags = payload.u8(0)
        return OutputProcessingConfig(
            mono = flags and 0x01 != 0,
            swap = flags and 0x02 != 0,
            invertLeft = flags and 0x04 != 0,
            invertRight = flags and 0x08 != 0,
            balancePercent = payload.i16(2) / 100.0,
            widthPercent = payload.u16(4) / 100.0,
        ).also(::validateOutputProcessing)
    }

    @Throws(ProtocolException::class)
    fun decodeOutputProcessingState(payload: ByteArray): OutputProcessingState {
        if (payload.size != 17 || payload.u8(16) !in 0..1) {
            throw ProtocolException("Invalid output processing state response")
        }
        return OutputProcessingState(
            live = decodeOutputProcessing(payload.copyOfRange(0, 8)),
            saved = decodeOutputProcessing(payload.copyOfRange(8, 16)),
            dirty = payload.u8(16) == 1,
        )
    }

    fun encodeOutputProcessingState(state: OutputProcessingState): ByteArray = ByteArray(17).also { payload ->
        encodeOutputProcessing(state.live).copyInto(payload, 0)
        encodeOutputProcessing(state.saved).copyInto(payload, 8)
        payload[16] = if (state.dirty) 1 else 0
    }

    private fun validateCrossfeed(config: CrossfeedConfig) {
        if (config.strengthPercent !in 0.0..40.0 ||
            config.cutoffHz !in 300..2_000 ||
            config.delayMs !in 0.0..0.6
        ) {
            throw ProtocolException("Crossfeed values are out of range")
        }
    }

    private fun validateOutputProcessing(config: OutputProcessingConfig) {
        if (!config.balancePercent.isFinite() || config.balancePercent !in -100.0..100.0 ||
            !config.widthPercent.isFinite() || config.widthPercent !in 0.0..200.0
        ) throw ProtocolException("Output processing values are out of range")
    }

    private fun statusLabel(response: ResponsePacket): String = when (response.status) {
        ProtocolStatus.Ok -> "OK"
        ProtocolStatus.InvalidPacket -> "The device rejected the packet"
        ProtocolStatus.InvalidCommand -> "The command is not supported"
        ProtocolStatus.InvalidLength -> "The command length is invalid"
        ProtocolStatus.InvalidIndex -> "The EQ band index is invalid"
        ProtocolStatus.OutOfRange -> "One or more EQ values are out of range"
        ProtocolStatus.Busy -> "The device is busy"
        ProtocolStatus.StorageError -> "The device could not write its flash storage"
        null -> "Unknown device error (${response.rawStatus})"
    }

    private fun milli(value: Double): Int = (value * 1000.0).roundToInt()
}

val DefaultEqConfig = EqConfig(
    enabled = true,
    preampDb = 0.0,
    bands = listOf(
        defaultBand(FilterType.Peaking, 68.0, 0.71, 1.89),
        defaultBand(FilterType.LowShelf, 105.0, 0.71, 1.89),
        defaultBand(FilterType.Peaking, 260.0, 4.0, 0.36),
        defaultBand(FilterType.Peaking, 1300.0, 3.0, 0.48),
        defaultBand(FilterType.Peaking, 1650.0, 3.0, 0.48),
        defaultBand(FilterType.Peaking, 2600.0, 5.0, 0.29),
        defaultBand(FilterType.HighShelf, 3000.0, 0.35, 3.33),
        defaultBand(FilterType.Peaking, 3000.0, 1.4, 1.01),
        defaultBand(FilterType.Peaking, 5100.0, 4.5, 0.32),
        defaultBand(FilterType.HighShelf, 10000.0, 0.71, 1.89),
    ),
)

val DefaultCrossfeedConfig = CrossfeedConfig(
    mode = CrossfeedMode.Off,
    strengthPercent = 20.0,
    cutoffHz = 700,
    delayMs = 0.25,
)

val DefaultOutputProcessingConfig = OutputProcessingConfig(
    mono = false,
    swap = false,
    invertLeft = false,
    invertRight = false,
    balancePercent = 0.0,
    widthPercent = 100.0,
)

fun CrossfeedMode.displayConfig(retainedCustom: CrossfeedConfig = DefaultCrossfeedConfig): CrossfeedConfig = when (this) {
    CrossfeedMode.Off -> retainedCustom.copy(mode = this, strengthPercent = 0.0, delayMs = 0.0)
    CrossfeedMode.Low -> retainedCustom.copy(mode = this, strengthPercent = 10.0, cutoffHz = 700, delayMs = 0.20)
    CrossfeedMode.Medium -> retainedCustom.copy(mode = this, strengthPercent = 20.0, cutoffHz = 700, delayMs = 0.25)
    CrossfeedMode.High -> retainedCustom.copy(mode = this, strengthPercent = 30.0, cutoffHz = 700, delayMs = 0.30)
    CrossfeedMode.Custom -> retainedCustom.copy(mode = this)
}

private fun defaultBand(type: FilterType, frequencyHz: Double, q: Double, bandwidth: Double) = EqBand(
    enabled = true,
    type = type,
    widthMode = if (type == FilterType.Peaking) WidthMode.Bandwidth else WidthMode.Q,
    frequencyHz = frequencyHz,
    gainDb = 0.0,
    q = q,
    bandwidthOctaves = bandwidth,
)

private fun ByteArray.buffer(): ByteBuffer = ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN)
private fun ByteArray.u8(offset: Int): Int = this[offset].toInt() and 0xff
private fun ByteArray.u16(offset: Int): Int = buffer().getShort(offset).toInt() and 0xffff
private fun ByteArray.i16(offset: Int): Int = buffer().getShort(offset).toInt()
private fun ByteArray.u32(offset: Int): Long = buffer().getInt(offset).toLong() and 0xffff_ffffL
private fun ByteArray.i32(offset: Int): Int = buffer().getInt(offset)
private fun ByteArray.putU16(offset: Int, value: Int) { buffer().putShort(offset, value.toShort()) }
private fun ByteArray.putI16(offset: Int, value: Int) { buffer().putShort(offset, value.toShort()) }
private fun ByteArray.putI32(offset: Int, value: Int) { buffer().putInt(offset, value) }
