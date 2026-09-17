package dev.lightwolf.pumper.controller.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.protocol.EqConfig
import dev.lightwolf.pumper.controller.protocol.EqMath
import dev.lightwolf.pumper.controller.protocol.ResponsePoint
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min

private val FrequencyGridTicks = listOf(
    20.0, 50.0, 100.0, 200.0, 500.0, 1_000.0, 2_000.0, 5_000.0, 10_000.0, 20_000.0,
)
private val FrequencyLabels = mapOf(
    20.0 to "20", 100.0 to "100", 1_000.0 to "1k", 10_000.0 to "10k", 20_000.0 to "20k",
)
internal val BandColors = listOf(
    androidx.compose.ui.graphics.Color(0xffef5350), androidx.compose.ui.graphics.Color(0xffff8a50),
    androidx.compose.ui.graphics.Color(0xffffc247), androidx.compose.ui.graphics.Color(0xff9ccc65),
    androidx.compose.ui.graphics.Color(0xff35b779), androidx.compose.ui.graphics.Color(0xff26c6da),
    androidx.compose.ui.graphics.Color(0xff42a5f5), androidx.compose.ui.graphics.Color(0xff7e8ce0),
    androidx.compose.ui.graphics.Color(0xffce74d9), androidx.compose.ui.graphics.Color(0xffec6ca5),
)

internal data class GainAxis(
    val minimum: Double,
    val maximum: Double,
    val tickStep: Double,
) {
    val ticks: List<Double>
        get() = buildList {
            var value = minimum
            while (value <= maximum + 0.001) {
                add(value)
                value += tickStep
            }
        }
}

internal fun calculateGainAxis(curve: List<ResponsePoint>): GainAxis {
    var minimum = min(0.0, curve.minOfOrNull { it.gainDb } ?: 0.0)
    var maximum = max(0.0, curve.maxOfOrNull { it.gainDb } ?: 0.0)
    if (maximum - minimum < 12.0) {
        val center = (minimum + maximum) / 2.0
        minimum = center - 6.0
        maximum = center + 6.0
        if (minimum > 0.0) {
            maximum -= minimum
            minimum = 0.0
        }
        if (maximum < 0.0) {
            minimum -= maximum
            maximum = 0.0
        }
    }
    minimum = floor(minimum / 3.0) * 3.0
    maximum = ceil(maximum / 3.0) * 3.0
    if (maximum - minimum < 12.0) maximum = minimum + 12.0
    val span = maximum - minimum
    val tickStep = listOf(3.0, 6.0, 12.0, 24.0, 48.0, 96.0).firstOrNull { span / it <= 8.0 } ?: 192.0
    return GainAxis(minimum, maximum, tickStep)
}

@Composable
fun EqGraph(
    config: EqConfig,
    sampleRateHz: Long,
    modifier: Modifier = Modifier,
) {
    val curve = remember(config, sampleRateHz) { EqMath.responseCurve(config, sampleRateHz) }
    val markers = remember(config, sampleRateHz) {
        config.bands.map { band ->
            val gainDb = if (config.enabled) {
                EqMath.compositeGainDb(config, sampleRateHz, band.frequencyHz) + config.preampDb
            } else {
                0.0
            }
            ResponsePoint(band.frequencyHz, gainDb)
        }
    }
    val axis = remember(curve) { calculateGainAxis(curve) }
    val textMeasurer = rememberTextMeasurer()
    val labelStyle = MaterialTheme.typography.labelSmall.copy(color = MaterialTheme.colorScheme.onSurfaceVariant)
    val gridColor = MaterialTheme.colorScheme.outlineVariant
    val zeroColor = MaterialTheme.colorScheme.outline
    val curveColor = MaterialTheme.colorScheme.primary
    val curveHalo = MaterialTheme.colorScheme.surface
    val backdrop = MaterialTheme.colorScheme.surfaceContainerLow

    Canvas(
        modifier
            .fillMaxWidth()
            .semantics { contentDescription = "Equalizer response from 20 hertz to 20 kilohertz" },
    ) {
        val plotLeft = 44.dp.toPx()
        val plotRight = size.width - 8.dp.toPx()
        val plotTop = 8.dp.toPx()
        val plotBottom = size.height - 30.dp.toPx()
        val plotWidth = plotRight - plotLeft
        val plotHeight = plotBottom - plotTop
        if (plotWidth <= 0f || plotHeight <= 0f) return@Canvas

        drawRoundRect(
            color = backdrop,
            topLeft = Offset(plotLeft, plotTop),
            size = Size(plotWidth, plotHeight),
            cornerRadius = CornerRadius(8.dp.toPx()),
        )
        FrequencyGridTicks.forEach { frequency ->
            val x = xForFrequency(frequency, plotLeft, plotWidth)
            drawLine(gridColor, Offset(x, plotTop), Offset(x, plotBottom), 1.dp.toPx())
        }
        axis.ticks.forEach { gain ->
            val y = yForGain(gain, axis, plotTop, plotHeight)
            drawLine(
                color = if (gain == 0.0) zeroColor else gridColor,
                start = Offset(plotLeft, y),
                end = Offset(plotRight, y),
                strokeWidth = if (gain == 0.0) 1.5.dp.toPx() else 1.dp.toPx(),
            )
            val label = when {
                gain > 0 -> "+${gain.toInt()}"
                else -> gain.toInt().toString()
            }
            val measured = textMeasurer.measure(AnnotatedString(label), labelStyle)
            drawText(
                measured,
                topLeft = Offset(plotLeft - measured.size.width - 7.dp.toPx(), y - measured.size.height / 2f),
            )
        }
        FrequencyLabels.forEach { (frequency, label) ->
            val x = xForFrequency(frequency, plotLeft, plotWidth)
            val measured = textMeasurer.measure(AnnotatedString(label), labelStyle)
            val labelX = (x - measured.size.width / 2f).coerceIn(0f, size.width - measured.size.width)
            drawText(measured, topLeft = Offset(labelX, plotBottom + 6.dp.toPx()))
        }

        val responsePath = Path()
        curve.forEachIndexed { index, point ->
            val x = xForFrequency(point.frequencyHz, plotLeft, plotWidth)
            val y = yForGain(point.gainDb, axis, plotTop, plotHeight)
            if (index == 0) responsePath.moveTo(x, y) else responsePath.lineTo(x, y)
        }
        drawPath(responsePath, curveHalo.copy(alpha = 0.82f), style = Stroke(7.dp.toPx()))
        drawPath(responsePath, curveColor, style = Stroke(3.dp.toPx()))
        markers.forEachIndexed { index, marker ->
            val enabled = config.enabled && config.bands[index].enabled
            val center = Offset(
                xForFrequency(marker.frequencyHz, plotLeft, plotWidth),
                yForGain(marker.gainDb, axis, plotTop, plotHeight),
            )
            drawCircle(
                color = curveHalo.copy(alpha = if (enabled) 0.9f else 0.55f),
                radius = 6.dp.toPx(),
                center = center,
            )
            drawCircle(
                color = BandColors[index % BandColors.size].copy(alpha = if (enabled) 1f else 0.34f),
                radius = 4.dp.toPx(),
                center = center,
            )
        }
    }
}

private fun xForFrequency(frequencyHz: Double, start: Float, width: Float): Float =
    (start + log10(frequencyHz / 20.0) / 3.0 * width).toFloat().coerceIn(start, start + width)

private fun yForGain(gainDb: Double, axis: GainAxis, start: Float, height: Float): Float =
    (start + (axis.maximum - gainDb) / (axis.maximum - axis.minimum) * height)
        .toFloat()
        .coerceIn(start, start + height)
