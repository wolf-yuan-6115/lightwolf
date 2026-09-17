package dev.lightwolf.pumper.controller.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class PumperHapticsTest {
    @Test
    fun sliderHapticsThrottlesDenseInputToBuckets() {
        val performer = RecordingSliderHaptics()
        val feedback = SliderHaptics(performer, bucketCount = 48)

        repeat(1_001) { step -> feedback.update(step / 1_000f) }
        feedback.finish()

        assertEquals(1, performer.starts)
        assertEquals(48, performer.ticks)
        assertEquals(1, performer.ends)
    }

    @Test
    fun sliderHapticsIgnoresRepeatedValuesAndDuplicateFinish() {
        val performer = RecordingSliderHaptics()
        val feedback = SliderHaptics(performer, bucketCount = 10)

        feedback.update(0.5f)
        feedback.update(0.5f)
        feedback.finish()
        feedback.finish()

        assertEquals(1, performer.starts)
        assertEquals(0, performer.ticks)
        assertEquals(1, performer.ends)
    }
}

private class RecordingSliderHaptics : SliderHapticPerformer {
    var starts = 0
    var ticks = 0
    var ends = 0

    override fun gestureStart() {
        starts += 1
    }

    override fun gestureTick() {
        ticks += 1
    }

    override fun gestureEnd() {
        ends += 1
    }
}
