package dev.lightwolf.pumper.controller.protocol

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.round
import kotlin.math.sin
import kotlin.math.sinh
import kotlin.math.sqrt

data class ResponsePoint(val frequencyHz: Double, val gainDb: Double)
data class AutoPreampResult(val preampDb: Double, val peakDb: Double)

private data class Coefficients(
    val b0: Double,
    val b1: Double,
    val b2: Double,
    val a1: Double,
    val a2: Double,
)

private val Identity = Coefficients(1.0, 0.0, 0.0, 0.0, 0.0)

object EqMath {
    fun compositeGainDb(config: EqConfig, sampleRateHz: Long, frequencyHz: Double): Double {
        if (!config.enabled) return 0.0
        return config.bands.sumOf { coefficientGainDb(buildCoefficients(it, sampleRateHz), frequencyHz, sampleRateHz) }
    }

    fun responseCurve(config: EqConfig, sampleRateHz: Long, count: Int = 512): List<ResponsePoint> {
        val chain = config.bands.map { buildCoefficients(it, sampleRateHz) }
        return List(count) { index ->
            val frequency = logFrequency(index, count)
            val gain = if (config.enabled) chainGainDb(chain, frequency, sampleRateHz) + config.preampDb else 0.0
            ResponsePoint(frequency, gain)
        }
    }

    fun calculateAutoPreamp(config: EqConfig, sampleRateHz: Long): AutoPreampResult {
        if (!config.enabled) return AutoPreampResult(0.0, 0.0)
        val chain = config.bands.map { buildCoefficients(it, sampleRateHz) }
        val count = 4096
        val frequencies = MutableList(count) { logFrequency(it, count) }
        frequencies += config.bands.filter { it.enabled }.map { it.frequencyHz }
        frequencies.sort()

        var peakDb = Double.NEGATIVE_INFINITY
        var peakIndex = 0
        frequencies.forEachIndexed { index, frequency ->
            val gain = chainGainDb(chain, frequency, sampleRateHz)
            if (gain > peakDb) {
                peakDb = gain
                peakIndex = index
            }
        }

        val lower = frequencies[max(0, peakIndex - 1)]
        val upper = frequencies[min(frequencies.lastIndex, peakIndex + 1)]
        repeat(257) { index ->
            val frequency = lower * (upper / lower).pow(index / 256.0)
            peakDb = max(peakDb, chainGainDb(chain, frequency, sampleRateHz))
        }
        val rounded = round(min(0.0, -peakDb) * 1000.0) / 1000.0
        return AutoPreampResult(if (rounded == -0.0) 0.0 else rounded, peakDb)
    }

    private fun buildCoefficients(band: EqBand, sampleRateHz: Long): Coefficients {
        if (!band.enabled || kotlin.math.abs(band.gainDb) < 0.0001 || band.frequencyHz >= sampleRateHz / 2.0) {
            return Identity
        }
        val w0 = 2.0 * PI * band.frequencyHz / sampleRateHz
        val sinW0 = sin(w0)
        val cosW0 = cos(w0)
        val a = 10.0.pow(band.gainDb / 40.0)
        val alpha: Double
        val b0: Double
        val b1: Double
        val b2: Double
        val a0: Double
        val a1: Double
        val a2: Double

        if (band.type == FilterType.Peaking) {
            alpha = if (band.widthMode == WidthMode.Bandwidth) {
                sinW0 * sinh((ln(2.0) / 2.0) * band.bandwidthOctaves * (w0 / sinW0))
            } else {
                sinW0 / (2.0 * band.q)
            }
            b0 = 1.0 + alpha * a
            b1 = -2.0 * cosW0
            b2 = 1.0 - alpha * a
            a0 = 1.0 + alpha / a
            a1 = -2.0 * cosW0
            a2 = 1.0 - alpha / a
        } else {
            alpha = (sinW0 / 2.0) * sqrt((a + 1.0 / a) * (1.0 / band.q - 1.0) + 2.0)
            val term = 2.0 * sqrt(a) * alpha
            if (band.type == FilterType.LowShelf) {
                b0 = a * (a + 1.0 - (a - 1.0) * cosW0 + term)
                b1 = 2.0 * a * (a - 1.0 - (a + 1.0) * cosW0)
                b2 = a * (a + 1.0 - (a - 1.0) * cosW0 - term)
                a0 = a + 1.0 + (a - 1.0) * cosW0 + term
                a1 = -2.0 * (a - 1.0 + (a + 1.0) * cosW0)
                a2 = a + 1.0 + (a - 1.0) * cosW0 - term
            } else {
                b0 = a * (a + 1.0 + (a - 1.0) * cosW0 + term)
                b1 = -2.0 * a * (a - 1.0 + (a + 1.0) * cosW0)
                b2 = a * (a + 1.0 + (a - 1.0) * cosW0 - term)
                a0 = a + 1.0 - (a - 1.0) * cosW0 + term
                a1 = 2.0 * (a - 1.0 - (a + 1.0) * cosW0)
                a2 = a + 1.0 - (a - 1.0) * cosW0 - term
            }
        }
        return Coefficients(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)
    }

    private fun coefficientGainDb(coefficients: Coefficients, frequencyHz: Double, sampleRateHz: Long): Double {
        val w = 2.0 * PI * frequencyHz / sampleRateHz
        val cos1 = cos(w)
        val sin1 = -sin(w)
        val cos2 = cos(2.0 * w)
        val sin2 = -sin(2.0 * w)
        val nr = coefficients.b0 + coefficients.b1 * cos1 + coefficients.b2 * cos2
        val ni = coefficients.b1 * sin1 + coefficients.b2 * sin2
        val dr = 1.0 + coefficients.a1 * cos1 + coefficients.a2 * cos2
        val di = coefficients.a1 * sin1 + coefficients.a2 * sin2
        return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di))
    }

    private fun chainGainDb(chain: List<Coefficients>, frequencyHz: Double, sampleRateHz: Long): Double =
        chain.sumOf { coefficientGainDb(it, frequencyHz, sampleRateHz) }

    private fun logFrequency(index: Int, count: Int): Double = 20.0 * 1000.0.pow(index.toDouble() / (count - 1))
}

