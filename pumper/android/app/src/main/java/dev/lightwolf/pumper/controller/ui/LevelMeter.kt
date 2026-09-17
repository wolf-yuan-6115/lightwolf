package dev.lightwolf.pumper.controller.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.protocol.MeterLevel
import dev.lightwolf.pumper.controller.protocol.StereoMeterLevel
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import java.util.Locale
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.log10
import kotlin.math.max

private const val MINIMUM_DB = -60f
private const val PEAK_HOLD_NANOS = 700_000_000L
private const val PEAK_DECAY_DB_PER_SECOND = 24f

internal data class MeterChannelModel(val rmsDb: Float = MINIMUM_DB, val peakDb: Float = MINIMUM_DB)

private data class MeterFrame(
    val inputLeft: MeterChannelModel = MeterChannelModel(),
    val inputRight: MeterChannelModel = MeterChannelModel(),
    val outputLeft: MeterChannelModel = MeterChannelModel(),
    val outputRight: MeterChannelModel = MeterChannelModel(),
)

internal data class PeakHoldState(var holdUntilNanos: Long = 0L)

internal fun peakToDb(peak: Int): Float = when {
    peak <= 0 -> MINIMUM_DB
    else -> (20.0 * log10(peak / 32768.0)).toFloat().coerceIn(MINIMUM_DB, 0f)
}

internal fun meanSquareToDb(meanSquare: Long): Float = when {
    meanSquare <= 0L -> MINIMUM_DB
    else -> (10.0 * log10(meanSquare / (32768.0 * 32768.0))).toFloat().coerceIn(MINIMUM_DB, 0f)
}

private fun normalized(db: Float): Float = ((db.coerceIn(MINIMUM_DB, 0f) - MINIMUM_DB) / -MINIMUM_DB)

@Composable
fun SignalLevels(
    levels: StateFlow<MeterLevel?>,
    modifier: Modifier = Modifier,
) {
    val frame = rememberAnimatedMeter(levels)

    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("Output", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
            Text("dBFS", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        StereoOutputField(frame)
        Text(
            "Input",
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        StereoInputField(frame)
    }
}

@Composable
private fun rememberAnimatedMeter(levels: StateFlow<MeterLevel?>): State<MeterFrame> {
    val animated = remember { mutableStateOf(MeterFrame()) }
    val peakHolds = remember { Array(4) { PeakHoldState() } }
    LaunchedEffect(levels) {
        levels.collectLatest { level ->
            val target = level.toMeterFrame()
            var frame = animated.value
            var previousNanos = withFrameNanos { it }
            do {
                withFrameNanos { now ->
                    val elapsedSeconds = ((now - previousNanos).coerceAtMost(100_000_000L) / 1_000_000_000f)
                    previousNanos = now
                    frame = MeterFrame(
                        advanceMeterChannel(frame.inputLeft, target.inputLeft, elapsedSeconds, now, peakHolds[0]),
                        advanceMeterChannel(frame.inputRight, target.inputRight, elapsedSeconds, now, peakHolds[1]),
                        advanceMeterChannel(frame.outputLeft, target.outputLeft, elapsedSeconds, now, peakHolds[2]),
                        advanceMeterChannel(frame.outputRight, target.outputRight, elapsedSeconds, now, peakHolds[3]),
                    )
                    if (animated.value != frame) animated.value = frame
                }
            } while (!meterSettled(frame, target))
        }
    }
    return animated
}

private fun meterSettled(current: MeterFrame, target: MeterFrame): Boolean =
    channelSettled(current.inputLeft, target.inputLeft) &&
        channelSettled(current.inputRight, target.inputRight) &&
        channelSettled(current.outputLeft, target.outputLeft) &&
        channelSettled(current.outputRight, target.outputRight)

private fun channelSettled(current: MeterChannelModel, target: MeterChannelModel): Boolean {
    val rmsSettled = abs(current.rmsDb - target.rmsDb) < 0.01f
    val peakSettled = abs(current.peakDb - target.peakDb) < 0.01f
    return rmsSettled && peakSettled
}

private fun MeterLevel?.toMeterFrame(): MeterFrame = if (this == null) {
    MeterFrame()
} else {
    MeterFrame(
        inputLeft = preEq.channel(left = true),
        inputRight = preEq.channel(left = false),
        outputLeft = postEq.channel(left = true),
        outputRight = postEq.channel(left = false),
    )
}

private fun StereoMeterLevel.channel(left: Boolean): MeterChannelModel = MeterChannelModel(
    rmsDb = meanSquareToDb(if (left) leftMeanSquare else rightMeanSquare),
    peakDb = peakToDb(if (left) leftPeak else rightPeak),
)

internal fun advanceMeterChannel(
    current: MeterChannelModel,
    target: MeterChannelModel,
    elapsedSeconds: Float,
    now: Long,
    peakHold: PeakHoldState,
): MeterChannelModel {
    val timeConstant = if (target.rmsDb > current.rmsDb) 0.02f else 0.1f
    val blend = (1f - exp(-elapsedSeconds / timeConstant)).coerceIn(0f, 1f)
    val rms = current.rmsDb + (target.rmsDb - current.rmsDb) * blend
    val peak = if (target.peakDb >= current.peakDb) {
        peakHold.holdUntilNanos = now + PEAK_HOLD_NANOS
        target.peakDb
    } else if (now < peakHold.holdUntilNanos) {
        current.peakDb
    } else {
        max(target.peakDb, current.peakDb - PEAK_DECAY_DB_PER_SECOND * elapsedSeconds)
    }
    return MeterChannelModel(rms.coerceIn(MINIMUM_DB, 0f), peak.coerceIn(MINIMUM_DB, 0f))
}

@Composable
private fun StereoOutputField(levels: State<MeterFrame>) {
    val leftColor = MaterialTheme.colorScheme.primary
    val rightColor = MaterialTheme.colorScheme.tertiary
    val trackColor = MaterialTheme.colorScheme.surfaceContainerHighest
    val centerColor = MaterialTheme.colorScheme.surface
    val errorColor = MaterialTheme.colorScheme.error
    val labelStyle = MaterialTheme.typography.labelLarge.copy(color = MaterialTheme.colorScheme.onSurfaceVariant)
    val valueStyle = MaterialTheme.typography.titleMedium.copy(color = MaterialTheme.colorScheme.onSurface)
    val scaleStyle = MaterialTheme.typography.labelSmall.copy(color = MaterialTheme.colorScheme.onSurfaceVariant)
    val textMeasurer = rememberTextMeasurer()

    Canvas(
        Modifier
            .fillMaxWidth()
            .height(126.dp)
            .semantics { contentDescription = "Stereo output level" },
    ) {
        val frame = levels.value
        val labelGap = 8.dp.toPx()
        val leftLabel = textMeasurer.measure(AnnotatedString("L"), style = labelStyle)
        val rightLabel = textMeasurer.measure(AnnotatedString("R"), style = labelStyle)
        val leftValue = textMeasurer.measure(AnnotatedString(formatDb(frame.outputLeft.peakDb)), style = valueStyle)
        val rightValue = textMeasurer.measure(AnnotatedString(formatDb(frame.outputRight.peakDb)), style = valueStyle)

        drawText(leftLabel, topLeft = Offset(0f, 0f))
        drawText(leftValue, topLeft = Offset(leftLabel.size.width + labelGap, 0f))
        drawText(rightLabel, topLeft = Offset(size.width - rightLabel.size.width, 0f))
        drawText(
            rightValue,
            topLeft = Offset(size.width - rightLabel.size.width - labelGap - rightValue.size.width, 0f),
        )

        val trackTop = 38.dp.toPx()
        val trackHeight = 34.dp.toPx()
        drawStereoField(
            left = frame.outputLeft,
            right = frame.outputRight,
            top = trackTop,
            height = trackHeight,
            leftColor = leftColor,
            rightColor = rightColor,
            trackColor = trackColor,
            centerColor = centerColor,
            errorColor = errorColor,
        )

        val scaleTop = 86.dp.toPx()
        drawScaleLabel("0", 0f, scaleTop, scaleStyle, textMeasurer, Alignment.Start)
        drawScaleLabel("-60", size.width / 2f, scaleTop, scaleStyle, textMeasurer)
        drawScaleLabel("0", size.width, scaleTop, scaleStyle, textMeasurer, Alignment.End)
    }
}

@Composable
private fun StereoInputField(levels: State<MeterFrame>) {
    val leftColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.58f)
    val rightColor = MaterialTheme.colorScheme.tertiary.copy(alpha = 0.58f)
    val trackColor = MaterialTheme.colorScheme.surfaceContainerHighest
    val centerColor = MaterialTheme.colorScheme.surface
    val errorColor = MaterialTheme.colorScheme.error
    val valueStyle = MaterialTheme.typography.labelSmall.copy(color = MaterialTheme.colorScheme.onSurfaceVariant)
    val textMeasurer = rememberTextMeasurer()

    Canvas(
        Modifier
            .fillMaxWidth()
            .height(48.dp)
            .semantics { contentDescription = "Stereo input level" },
    ) {
        val frame = levels.value
        val leftText = textMeasurer.measure(AnnotatedString("L  ${formatDb(frame.inputLeft.peakDb)}"), style = valueStyle)
        val rightText = textMeasurer.measure(AnnotatedString("${formatDb(frame.inputRight.peakDb)}  R"), style = valueStyle)
        drawText(leftText, topLeft = Offset.Zero)
        drawText(rightText, topLeft = Offset(size.width - rightText.size.width, 0f))
        drawStereoField(
            left = frame.inputLeft,
            right = frame.inputRight,
            top = 28.dp.toPx(),
            height = 10.dp.toPx(),
            leftColor = leftColor,
            rightColor = rightColor,
            trackColor = trackColor,
            centerColor = centerColor,
            errorColor = errorColor,
        )
    }
}

private fun DrawScope.drawStereoField(
    left: MeterChannelModel,
    right: MeterChannelModel,
    top: Float,
    height: Float,
    leftColor: Color,
    rightColor: Color,
    trackColor: Color,
    centerColor: Color,
    errorColor: Color,
) {
    val center = size.width / 2f
    val radius = height / 2f
    drawRoundRect(trackColor, Offset(0f, top), Size(size.width, height), CornerRadius(radius))

    val leftWidth = center * normalized(left.rmsDb)
    val rightWidth = center * normalized(right.rmsDb)
    if (leftWidth > 0f) {
        drawRoundRect(leftColor, Offset(center - leftWidth, top), Size(leftWidth, height), CornerRadius(radius))
        val squareWidth = minOf(radius, leftWidth / 2f)
        drawRect(leftColor, Offset(center - squareWidth, top), Size(squareWidth, height))
    }
    if (rightWidth > 0f) {
        drawRoundRect(rightColor, Offset(center, top), Size(rightWidth, height), CornerRadius(radius))
        val squareWidth = minOf(radius, rightWidth / 2f)
        drawRect(rightColor, Offset(center, top), Size(squareWidth, height))
    }

    drawLine(centerColor, Offset(center, top + 3.dp.toPx()), Offset(center, top + height - 3.dp.toPx()), 2.dp.toPx())
    drawPeakMarker(left, stereoX(left.peakDb, left = true), top, height, leftColor, errorColor)
    drawPeakMarker(right, stereoX(right.peakDb, left = false), top, height, rightColor, errorColor)
}

private fun DrawScope.drawPeakMarker(
    channel: MeterChannelModel,
    x: Float,
    top: Float,
    height: Float,
    color: Color,
    errorColor: Color,
) {
    if (channel.peakDb <= MINIMUM_DB + 0.05f) return
    val markerColor = if (channel.peakDb >= -1f) errorColor else color
    drawLine(
        markerColor,
        Offset(x, top - 3.dp.toPx()),
        Offset(x, top + height + 3.dp.toPx()),
        3.dp.toPx(),
        StrokeCap.Round,
    )
}

private fun DrawScope.stereoX(db: Float, left: Boolean): Float {
    val distance = size.width / 2f * normalized(db)
    return size.width / 2f + if (left) -distance else distance
}

private fun DrawScope.drawScaleLabel(
    text: String,
    anchorX: Float,
    top: Float,
    style: androidx.compose.ui.text.TextStyle,
    textMeasurer: androidx.compose.ui.text.TextMeasurer,
    alignment: Alignment.Horizontal = Alignment.CenterHorizontally,
) {
    val layout = textMeasurer.measure(AnnotatedString(text), style = style)
    val x = when (alignment) {
        Alignment.Start -> anchorX
        Alignment.End -> anchorX - layout.size.width
        else -> anchorX - layout.size.width / 2f
    }
    drawText(layout, topLeft = Offset(x, top))
}

private fun formatDb(db: Float): String = if (db <= MINIMUM_DB + 0.05f) {
    "-inf"
} else {
    String.format(Locale.US, "%.1f", db)
}
