package dev.wolf.yuan.pumper.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import dev.wolf.yuan.pumper.protocol.SPECTRUM_STALE_MS
import dev.wolf.yuan.pumper.protocol.SpectrumFrame
import dev.wolf.yuan.pumper.protocol.smoothSpectrum
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlin.math.abs

@Composable
internal fun rememberSpectrumAnimation(source: StateFlow<SpectrumFrame?>?): State<FloatArray?> {
    val displayed = remember(source) { mutableStateOf<FloatArray?>(null) }
    LaunchedEffect(source) {
        var epoch: Long? = null
        var previousTime = 0L
        source?.collectLatest { frame ->
            if (frame == null) {
                displayed.value = null
                previousTime = 0
                epoch = null
                return@collectLatest
            }
            if (epoch != frame.epoch) {
                displayed.value = null
                previousTime = 0
                epoch = frame.epoch
            }
            while (System.nanoTime() / 1_000_000 - frame.receivedAtMs <= SPECTRUM_STALE_MS) {
                withFrameNanos { now ->
                    val elapsed = if (previousTime == 0L) 16.0 else (now - previousTime) / 1_000_000.0
                    previousTime = now
                    val old = displayed.value ?: FloatArray(256) { -96f }
                    if (old.indices.any { abs(old[it] - (frame.levels[it] / 2f - 96f)) >= 0.05f }) {
                        displayed.value = smoothSpectrum(old, frame.levels, elapsed)
                    }
                }
            }
            // Keep assembler replay protection; only the visual expires.
            displayed.value = null
            previousTime = 0
        }
    }
    return displayed
}
