package dev.lightwolf.pumper.controller.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Save
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.ControllerUiState
import dev.lightwolf.pumper.controller.protocol.CrossfeedConfig
import dev.lightwolf.pumper.controller.protocol.CrossfeedMode
import dev.lightwolf.pumper.controller.protocol.DefaultCrossfeedConfig
import dev.lightwolf.pumper.controller.protocol.displayConfig
import java.text.DecimalFormat
import java.text.DecimalFormatSymbols
import java.util.Locale
import kotlin.math.abs
import kotlin.math.round

@Composable
fun AudioScreen(
    state: ControllerUiState,
    onCrossfeedChange: (CrossfeedConfig) -> Unit,
    onSaveCrossfeed: () -> Unit,
) {
    BoxWithConstraints(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        val padding = if (maxWidth >= 600.dp) PumperSpacing.large else PumperSpacing.medium
        val expanded = maxWidth >= 840.dp
        val supported = state.status?.supportsAudioControls == true
        if (expanded) {
            Row(
                Modifier.widthIn(max = PumperMaxContentWidth).fillMaxWidth()
                    .verticalScroll(rememberScrollState()).padding(padding),
                horizontalArrangement = Arrangement.spacedBy(PumperSpacing.medium),
            ) {
                UsbAudioCard(state, supported, Modifier.weight(1f))
                CrossfeedCard(
                    state = state,
                    supported = supported,
                    onChange = onCrossfeedChange,
                    onSave = onSaveCrossfeed,
                    modifier = Modifier.weight(1f),
                )
            }
        } else {
            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(padding),
                verticalArrangement = Arrangement.spacedBy(PumperSpacing.medium),
            ) {
                UsbAudioCard(state, supported)
                CrossfeedCard(state, supported, onCrossfeedChange, onSaveCrossfeed)
            }
        }
    }
}

@Composable
private fun UsbAudioCard(state: ControllerUiState, supported: Boolean, modifier: Modifier = Modifier) {
    AudioCard(modifier.testTag("usb-audio-card")) {
        CardTitle("USB audio")
        if (!supported) {
            RequirementNotice()
            return@AudioCard
        }
        val status = state.status
        val master = state.audioControls?.master
        AudioValueRow(
            label = "Sample rate",
            value = status?.sampleRateHz?.let(::formatSampleRate) ?: "-",
        )
        AudioValueRow(
            label = "Stream state",
            value = if (status?.streaming == true) "Active" else "Idle",
        )
        AudioValueRow(
            label = "Master volume",
            value = master?.let {
                val volume = "${formatDb(it.volumeDb)} dB"
                if (it.muted) "Muted · $volume" else volume
            } ?: "-",
        )
    }
}

@Composable
private fun CrossfeedCard(
    state: ControllerUiState,
    supported: Boolean,
    onChange: (CrossfeedConfig) -> Unit,
    onSave: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val haptics = rememberPumperHaptics()
    AudioCard(modifier.testTag("crossfeed-card")) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            CardTitle("Headphone crossfeed", Modifier.weight(1f))
            if (supported) SavedPill(state.crossfeed?.dirty == true)
        }
        if (!supported) {
            RequirementNotice()
            return@AudioCard
        }
        val retained = state.crossfeed?.live ?: DefaultCrossfeedConfig
        val display = retained.mode.displayConfig(retained)
        androidx.compose.foundation.layout.FlowRow(
            horizontalArrangement = Arrangement.spacedBy(PumperSpacing.small),
            verticalArrangement = Arrangement.spacedBy(PumperSpacing.extraSmall),
            modifier = Modifier.fillMaxWidth().testTag("crossfeed-modes"),
        ) {
            CrossfeedMode.entries.forEach { mode ->
                FilterChip(
                    selected = retained.mode == mode,
                    onClick = {
                        haptics.selection()
                        onChange(retained.copy(mode = mode))
                    },
                    label = { Text(mode.name) },
                    enabled = !state.crossfeedSaving && !state.busy,
                )
            }
        }
        val editable = retained.mode == CrossfeedMode.Custom && !state.crossfeedSaving && !state.busy
        CrossfeedSlider(
            label = "Strength",
            value = display.strengthPercent,
            range = 0.0..40.0,
            step = 1.0,
            suffix = "%",
            enabled = editable,
            onValue = { onChange(retained.copy(strengthPercent = it)) },
        )
        CrossfeedSlider(
            label = "Cutoff",
            value = display.cutoffHz.toDouble(),
            range = 300.0..2_000.0,
            step = 10.0,
            suffix = "Hz",
            enabled = editable,
            onValue = { onChange(retained.copy(cutoffHz = it.toInt())) },
        )
        CrossfeedSlider(
            label = "Delay",
            value = display.delayMs,
            range = 0.0..0.6,
            step = 0.01,
            suffix = "ms",
            enabled = editable,
            decimals = 2,
            onValue = { onChange(retained.copy(delayMs = it)) },
        )
        Button(
            onClick = {
                haptics.confirm()
                onSave()
            },
            enabled = state.crossfeed?.dirty == true && !state.crossfeedSaving && !state.busy,
            shapes = ButtonDefaults.shapes(),
            modifier = Modifier.align(Alignment.End).testTag("save-crossfeed"),
        ) {
            Icon(Icons.Outlined.Save, contentDescription = null)
            Spacer(Modifier.width(PumperSpacing.small))
            Text(if (state.crossfeedSaving) "Saving" else "Save crossfeed")
        }
    }
}

@Composable
private fun AudioCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
    ) {
        Column(
            Modifier.fillMaxWidth().padding(PumperSpacing.medium),
            verticalArrangement = Arrangement.spacedBy(PumperSpacing.medium),
            content = content,
        )
    }
}

@Composable
private fun CardTitle(title: String, modifier: Modifier = Modifier) {
    Text(title, modifier = modifier, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
}

@Composable
private fun AudioValueRow(label: String, value: String) {
    Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = PumperSpacing.medium, vertical = PumperSpacing.small),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
            Text(value, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold)
        }
    }
}

@Composable
private fun SavedPill(dirty: Boolean) {
    Surface(
        shape = MaterialTheme.shapes.extraLarge,
        color = if (dirty) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.tertiaryContainer,
    ) {
        Text(
            if (dirty) "Unsaved" else "Saved",
            modifier = Modifier.padding(horizontal = PumperSpacing.small, vertical = PumperSpacing.extraSmall),
            color = if (dirty) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onTertiaryContainer,
            style = MaterialTheme.typography.labelMedium,
        )
    }
}

@Composable
private fun RequirementNotice() {
    Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer) {
        Text(
            "Requires firmware 2.2",
            Modifier.fillMaxWidth().padding(PumperSpacing.medium),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun CrossfeedSlider(
    label: String,
    value: Double,
    range: ClosedFloatingPointRange<Double>,
    step: Double,
    suffix: String,
    enabled: Boolean,
    decimals: Int = 0,
    onValue: (Double) -> Unit,
) {
    var editing by remember { mutableStateOf(false) }
    val sliderHaptics = rememberSliderHaptics(((range.endInclusive - range.start) / step).toInt())
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            TextButton(onClick = { editing = true }, enabled = enabled, shapes = ButtonDefaults.shapes()) {
                Text("${formatNumber(value, decimals)} $suffix")
            }
        }
        Slider(
            value = value.toFloat(),
            onValueChange = { raw ->
                sliderHaptics.update(((raw - range.start) / (range.endInclusive - range.start)).toFloat())
                val snapped = round((raw - range.start) / step) * step + range.start
                onValue(snapped.coerceIn(range))
            },
            onValueChangeFinished = sliderHaptics::finish,
            valueRange = range.start.toFloat()..range.endInclusive.toFloat(),
            enabled = enabled,
            modifier = Modifier.testTag("crossfeed-${label.lowercase()}")
        )
    }
    if (editing) {
        CrossfeedNumberDialog(
            label = label,
            value = value,
            range = range,
            step = step,
            suffix = suffix,
            decimals = decimals,
            onDismiss = { editing = false },
            onValue = {
                onValue(it)
                editing = false
            },
        )
    }
}

@Composable
private fun CrossfeedNumberDialog(
    label: String,
    value: Double,
    range: ClosedFloatingPointRange<Double>,
    step: Double,
    suffix: String,
    decimals: Int,
    onDismiss: () -> Unit,
    onValue: (Double) -> Unit,
) {
    var text by remember(value) { mutableStateOf(formatNumber(value, decimals)) }
    val haptics = rememberPumperHaptics()
    val parsed = text.toDoubleOrNull()
    val aligned = parsed != null && abs(((parsed - range.start) / step) - round((parsed - range.start) / step)) < 0.0001
    val valid = parsed != null && parsed in range && aligned
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(label) },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                isError = text.isNotBlank() && !valid,
                singleLine = true,
                suffix = { Text(suffix) },
                supportingText = if (!valid) ({ Text("Enter ${formatNumber(range.start, decimals)}–${formatNumber(range.endInclusive, decimals)} in $step steps") }) else null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    if (valid) {
                        haptics.confirm()
                        onValue(requireNotNull(parsed))
                    }
                }),
            )
        },
        confirmButton = {
            Button(onClick = {
                haptics.confirm()
                onValue(requireNotNull(parsed))
            }, enabled = valid, shapes = ButtonDefaults.shapes()) {
                Text("Apply")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

private val AudioNumberFormat = DecimalFormat("0.##", DecimalFormatSymbols(Locale.US))
private fun formatNumber(value: Double, decimals: Int): String = synchronized(AudioNumberFormat) {
    AudioNumberFormat.minimumFractionDigits = decimals
    AudioNumberFormat.maximumFractionDigits = decimals
    AudioNumberFormat.format(value)
}

private fun formatDb(value: Double): String = if (value == 0.0) "0" else "−${formatNumber(abs(value), 0)}"

private fun formatSampleRate(value: Long): String {
    val khz = value / 1_000.0
    return "${formatNumber(khz, if (value % 1_000L == 0L) 0 else 1)} kHz"
}
