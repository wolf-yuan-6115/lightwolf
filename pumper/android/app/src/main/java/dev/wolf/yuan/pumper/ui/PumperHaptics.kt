package dev.wolf.yuan.pumper.ui

import android.view.HapticFeedbackConstants
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalView
import kotlin.math.roundToInt

@Stable
internal class PumperHaptics(private val view: View) : SliderHapticPerformer {
    fun selection() = perform(HapticFeedbackConstants.SEGMENT_TICK)

    fun toggle(checked: Boolean) = perform(
        if (checked) HapticFeedbackConstants.TOGGLE_ON else HapticFeedbackConstants.TOGGLE_OFF,
    )

    fun confirm() = perform(HapticFeedbackConstants.CONFIRM)

    fun reject() = perform(HapticFeedbackConstants.REJECT)

    override fun gestureStart() = perform(HapticFeedbackConstants.GESTURE_START)

    override fun gestureTick() = perform(HapticFeedbackConstants.SEGMENT_FREQUENT_TICK)

    override fun gestureEnd() = perform(HapticFeedbackConstants.GESTURE_END)

    private fun perform(feedbackConstant: Int) {
        view.performHapticFeedback(feedbackConstant)
    }
}

internal interface SliderHapticPerformer {
    fun gestureStart()
    fun gestureTick()
    fun gestureEnd()
}

@Composable
internal fun rememberPumperHaptics(): PumperHaptics {
    val view = LocalView.current
    return remember(view) { PumperHaptics(view) }
}

@Stable
internal class SliderHaptics(
    private val haptics: SliderHapticPerformer,
    private val bucketCount: Int,
) {
    private var active = false
    private var lastBucket: Int? = null

    fun update(fraction: Float) {
        val bucket = (fraction.coerceIn(0f, 1f) * bucketCount).roundToInt()
        if (!active) {
            active = true
            lastBucket = bucket
            haptics.gestureStart()
        } else if (bucket != lastBucket) {
            lastBucket = bucket
            haptics.gestureTick()
        }
    }

    fun finish() {
        if (!active) return
        active = false
        lastBucket = null
        haptics.gestureEnd()
    }
}

@Composable
internal fun rememberSliderHaptics(bucketCount: Int = 48): SliderHaptics {
    val haptics = rememberPumperHaptics()
    return remember(haptics, bucketCount) { SliderHaptics(haptics, bucketCount) }
}
