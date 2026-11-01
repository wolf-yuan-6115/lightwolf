package dev.wolf.yuan.pumper.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.exp

const val SPECTRUM_STALE_MS = 500L

data class SpectrumFrame(val levels: List<Int>, val receivedAtMs: Long, val epoch: Long)
data class SpectrumChunk(val sequence: Long, val rate: Long, val index: Int, val levels: List<Int>)

fun decodeSpectrumChunk(payload: ByteArray): SpectrumChunk {
    require(payload.size >= 12) { "Truncated spectrum report" }
    val index = payload[1].toInt() and 255
    val count = if (index == 5) 36 else 44
    require(payload[0].toInt() == 1 && index in 0..5 && payload[2].toInt() == 6 &&
        payload[3].toInt() == count && payload.size == 12 + count) { "Invalid spectrum header" }
    val buffer = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
    val rate = buffer.getInt(8).toLong() and 0xffffffffL
    val levels = payload.drop(12).map { it.toInt() and 255 }
    require(rate == 44100L || rate == 48000L) { "Invalid spectrum rate" }
    require(levels.all { it in 0..192 }) { "Invalid spectrum level" }
    return SpectrumChunk(buffer.getInt(4).toLong() and 0xffffffffL, rate, index, levels)
}

class SpectrumAssembler {
    private var sequence: Long? = null
    private var started = 0L
    private var rate = 0L
    private var mask = 0
    private var invalid = false
    private val levels = IntArray(256)

    fun push(chunk: SpectrumChunk, nowMs: Long): List<Int>? {
        sequence?.let { previous ->
            if (chunk.sequence != previous && ((chunk.sequence - previous) and 0xffffffffL) >= 0x80000000L) return null
        }
        if (sequence != chunk.sequence) {
            sequence = chunk.sequence
            started = nowMs
            rate = chunk.rate
            mask = 0
            invalid = false
        }
        if (invalid || nowMs - started > 250 || chunk.rate != rate || mask == 63) return null
        val offset = chunk.index * 44
        if (mask and (1 shl chunk.index) != 0) {
            if (chunk.levels.indices.any { levels[offset + it] != chunk.levels[it] }) invalid = true
            return null
        }
        chunk.levels.forEachIndexed { index, value -> levels[offset + index] = value }
        mask = mask or (1 shl chunk.index)
        return if (mask == 63) levels.toList() else null
    }
}

/** dB-height interpolation, matching the web's 8 ms attack / 70 ms release. */
fun smoothSpectrum(previous: FloatArray, levels: List<Int>, elapsedMs: Double): FloatArray =
    FloatArray(256) { index ->
        val target = levels[index] / 2f - 96f
        val old = previous[index]
        val alpha = 1 - exp(-elapsedMs.coerceAtLeast(0.0) / if (target > old) 8.0 else 70.0)
        val next = (old + (target - old) * alpha).toFloat()
        if (abs(target - next) < 0.05f) target else next
    }
