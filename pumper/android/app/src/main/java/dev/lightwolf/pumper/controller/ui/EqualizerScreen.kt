@file:OptIn(androidx.compose.material3.ExperimentalMaterial3ExpressiveApi::class)

package dev.lightwolf.pumper.controller.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.ChevronLeft
import androidx.compose.material.icons.outlined.ChevronRight
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.ControllerUiState
import dev.lightwolf.pumper.controller.PumperControllerViewModel
import dev.lightwolf.pumper.controller.protocol.EqBand
import dev.lightwolf.pumper.controller.protocol.FilterType
import dev.lightwolf.pumper.controller.protocol.WidthMode
import java.text.DecimalFormat
import java.text.DecimalFormatSymbols
import java.util.Locale
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.round

@Composable
fun EqualizerScreen(state: ControllerUiState, controller: PumperControllerViewModel) {
    var editorIndex by rememberSaveable { mutableStateOf<Int?>(null) }
    var profileActionsIndex by rememberSaveable { mutableStateOf<Int?>(null) }
    BoxWithConstraints(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        val expandedLayout = maxWidth >= 840.dp
        val contentPadding = if (maxWidth >= 600.dp) PumperSpacing.large else PumperSpacing.medium
        val graphHeight = if (expandedLayout) 360.dp else 300.dp
        val contentModifier = Modifier
            .widthIn(max = PumperMaxContentWidth)
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(contentPadding)

        if (expandedLayout) {
            Row(
                contentModifier,
                horizontalArrangement = Arrangement.spacedBy(PumperSpacing.large),
                verticalAlignment = Alignment.Top,
            ) {
                EqualizerOverview(
                    state = state,
                    controller = controller,
                    graphHeight = graphHeight,
                    onManageProfile = { profileActionsIndex = it },
                    modifier = Modifier.weight(3f),
                )
                BandsSection(
                    state = state,
                    onOpen = { index ->
                        controller.selectBand(index)
                        editorIndex = index
                    },
                    modifier = Modifier.weight(2f),
                )
            }
        } else {
            Column(contentModifier, verticalArrangement = Arrangement.spacedBy(PumperSpacing.large)) {
                EqualizerOverview(
                    state = state,
                    controller = controller,
                    graphHeight = graphHeight,
                    onManageProfile = { profileActionsIndex = it },
                )
                BandsSection(
                    state = state,
                    onOpen = { index ->
                        controller.selectBand(index)
                        editorIndex = index
                    },
                )
                Spacer(Modifier.size(PumperSpacing.small))
            }
        }
    }

    editorIndex?.let { index ->
        val band = state.config.bands.getOrNull(index)
        if (band == null) {
            editorIndex = null
        } else {
            BandEditorSheet(
                index = index,
                band = band,
                bandCount = state.config.bands.size,
                supportsFirmware3Controls = state.status?.supportsFirmware3Controls == true,
                controller = controller,
                onSelect = { next ->
                    controller.selectBand(next)
                    editorIndex = next
                },
                onDismiss = { editorIndex = null },
            )
        }
    }

    profileActionsIndex?.let { index ->
        ProfileActionsDialog(
            index = index,
            state = state,
            onDismiss = { profileActionsIndex = null },
            onMakeDefault = {
                profileActionsIndex = null
                controller.makeProfileDefault(index)
            },
            onDelete = {
                profileActionsIndex = null
                controller.deleteProfile(index)
            },
        )
    }
}

@Composable
private fun EqualizerOverview(
    state: ControllerUiState,
    controller: PumperControllerViewModel,
    graphHeight: Dp,
    onManageProfile: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(PumperSpacing.large)) {
        ProfileSelector(state = state, controller = controller, onManage = onManageProfile)
        Surface(
            shape = MaterialTheme.shapes.extraLarge,
            color = MaterialTheme.colorScheme.surfaceContainerLow,
            modifier = Modifier.fillMaxWidth(),
        ) {
            GlobalEqControls(
                state = state,
                controller = controller,
                modifier = Modifier.padding(PumperSpacing.medium),
            )
        }
        Text("Response", style = MaterialTheme.typography.titleLargeEmphasized)
        EqGraph(
            config = state.config,
            sampleRateHz = state.status?.sampleRateHz ?: 48_000,
            modifier = Modifier.fillMaxWidth().height(graphHeight),
        )
    }
}

@Composable
private fun BandsSection(
    state: ControllerUiState,
    onOpen: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(PumperSpacing.large)) {
        Text("Bands", style = MaterialTheme.typography.titleLargeEmphasized)
        BandList(state = state, onOpen = onOpen)
    }
}

@Composable
private fun GlobalEqControls(
    state: ControllerUiState,
    controller: PumperControllerViewModel,
    modifier: Modifier = Modifier,
) {
    var editPreamp by rememberSaveable { mutableStateOf(false) }
    val haptics = rememberPumperHaptics()
    val sliderHaptics = rememberSliderHaptics(bucketCount = 36)
    Column(modifier, verticalArrangement = Arrangement.spacedBy(PumperSpacing.small)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Equalizer", style = MaterialTheme.typography.titleMediumEmphasized)
                Text(
                    if (state.hasUnsavedEdits) "Unsaved live changes" else "Matches stored profile",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Switch(
                checked = state.config.enabled,
                onCheckedChange = {
                    haptics.toggle(it)
                    controller.updateGlobal(enabled = it)
                },
            )
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("Preamp", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
            Text("Auto", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.width(PumperSpacing.small))
            Switch(
                checked = state.autoPreamp,
                onCheckedChange = {
                    haptics.toggle(it)
                    controller.setAutoPreamp(it)
                },
            )
            TextButton(
                onClick = {
                    haptics.selection()
                    editPreamp = true
                },
                enabled = !state.autoPreamp,
                shapes = ButtonDefaults.shapes(),
            ) {
                Text("${formatNumber(state.config.preampDb)} dB")
            }
        }
        Slider(
            value = state.config.preampDb.coerceIn(-24.0, 12.0).toFloat(),
            onValueChange = {
                sliderHaptics.update((it + 24f) / 36f)
                controller.updateGlobal(preampDb = round(it * 10.0) / 10.0)
            },
            onValueChangeFinished = sliderHaptics::finish,
            valueRange = -24f..12f,
            enabled = !state.autoPreamp,
        )
    }
    if (editPreamp) {
        NumberEntryDialog(
            title = "Preamp",
            value = state.config.preampDb,
            minimum = -241.0,
            maximum = 12.0,
            suffix = "dB",
            onDismiss = { editPreamp = false },
            onValue = {
                controller.updateGlobal(preampDb = it)
                editPreamp = false
            },
        )
    }
}

@Composable
private fun BandList(state: ControllerUiState, onOpen: (Int) -> Unit) {
    val haptics = rememberPumperHaptics()
    Column(
        Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(PumperSpacing.extraSmall),
    ) {
        state.config.bands.forEachIndexed { index, band ->
            val bandColor = BandColors[index % BandColors.size]
            val numberColor = when {
                !band.enabled -> MaterialTheme.colorScheme.onSurfaceVariant
                bandColor.luminance() > 0.45f -> Color.Black
                else -> Color.White
            }
            SegmentedListItem(
                onClick = {
                    haptics.selection()
                    onOpen(index)
                },
                shapes = ListItemDefaults.segmentedShapes(index, state.config.bands.size),
                modifier = Modifier.fillMaxWidth(),
                leadingContent = {
                    Box(
                        Modifier
                            .size(28.dp)
                            .background(
                                bandColor.copy(alpha = if (band.enabled) 1f else 0.32f),
                                CircleShape,
                            ),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(
                            "${index + 1}",
                            style = MaterialTheme.typography.labelMedium,
                            color = numberColor,
                        )
                    }
                },
                supportingContent = {
                    val gain = if (band.type.isGainIndependent()) "" else " | ${formatSigned(band.gainDb)} dB"
                    Text("${filterLabel(band.type)} | ${formatFrequency(band.frequencyHz)}$gain" + if (band.enabled) "" else " | Off")
                },
                trailingContent = { Icon(Icons.Outlined.ChevronRight, contentDescription = "Edit band ${index + 1}") },
            ) {
                Text("Band ${index + 1}", style = MaterialTheme.typography.bodyLarge)
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BandEditorSheet(
    index: Int,
    band: EqBand,
    bandCount: Int,
    supportsFirmware3Controls: Boolean,
    controller: PumperControllerViewModel,
    onSelect: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = false)
    val haptics = rememberPumperHaptics()
    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = sheetState) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .navigationBarsPadding()
                .padding(horizontal = PumperSpacing.large)
                .padding(bottom = PumperSpacing.large),
            verticalArrangement = Arrangement.spacedBy(PumperSpacing.medium),
        ) {
            Box(Modifier.fillMaxWidth()) {
                IconButton(
                    onClick = {
                        haptics.selection()
                        onSelect(index - 1)
                    },
                    enabled = index > 0,
                    shapes = IconButtonDefaults.shapes(),
                    modifier = Modifier.align(Alignment.CenterStart),
                ) {
                    Icon(Icons.Outlined.ChevronLeft, contentDescription = "Previous band")
                }
                Text(
                    "Band ${index + 1}",
                    style = MaterialTheme.typography.titleLargeEmphasized,
                    modifier = Modifier.align(Alignment.Center),
                )
                IconButton(
                    onClick = {
                        haptics.selection()
                        onSelect(index + 1)
                    },
                    enabled = index < bandCount - 1,
                    shapes = IconButtonDefaults.shapes(),
                    modifier = Modifier.align(Alignment.CenterEnd),
                ) {
                    Icon(Icons.Outlined.ChevronRight, contentDescription = "Next band")
                }
            }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("Enabled", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                Switch(
                    checked = band.enabled,
                    onCheckedChange = { enabled ->
                        haptics.toggle(enabled)
                        controller.updateBand(index) { it.copy(enabled = enabled) }
                    },
                )
            }
            FilterTypeDropdown(
                selected = band.type,
                supportsFirmware3Controls = supportsFirmware3Controls,
                onSelect = { type ->
                    if (band.type != type) {
                        haptics.selection()
                        controller.updateBand(index) { it.copy(type = type) }
                    }
                },
            )
            ValueSlider(
                label = "Frequency",
                value = band.frequencyHz,
                minimum = 20.0,
                maximum = 20_000.0,
                step = 1.0,
                suffix = "Hz",
                logarithmic = true,
                onValue = { value -> controller.updateBand(index) { it.copy(frequencyHz = value) } },
            )
            ValueSlider(
                label = "Gain",
                value = band.gainDb,
                minimum = -24.0,
                maximum = 24.0,
                step = 0.1,
                suffix = "dB",
                enabled = !band.type.isGainIndependent(),
                onValue = { value -> controller.updateBand(index) { it.copy(gainDb = value) } },
            )
            if (band.type == FilterType.Peaking) {
                Text("Width", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                    val options = listOf(WidthMode.Bandwidth to "Bandwidth", WidthMode.Q to "Q")
                    options.forEachIndexed { optionIndex, (mode, label) ->
                        SegmentedButton(
                            selected = band.widthMode == mode,
                            onClick = {
                                if (band.widthMode != mode) {
                                    haptics.selection()
                                    controller.updateBand(index) { it.copy(widthMode = mode) }
                                }
                            },
                            shape = SegmentedButtonDefaults.itemShape(optionIndex, options.size),
                            label = { Text(label) },
                        )
                    }
                }
            }
            if (band.type == FilterType.Peaking && band.widthMode == WidthMode.Bandwidth) {
                ValueSlider(
                    label = "Bandwidth",
                    value = band.bandwidthOctaves,
                    minimum = 0.1,
                    maximum = 4.0,
                    step = 0.01,
                    suffix = "oct",
                    onValue = { value -> controller.updateBand(index) { it.copy(bandwidthOctaves = value) } },
                )
            } else {
                ValueSlider(
                    label = if (band.type == FilterType.LowShelf || band.type == FilterType.HighShelf) "Slope" else "Q",
                    value = band.q,
                    minimum = 0.1,
                    maximum = if (band.type == FilterType.LowShelf || band.type == FilterType.HighShelf) 1.0 else 20.0,
                    step = 0.01,
                    suffix = "",
                    onValue = { value -> controller.updateBand(index) { it.copy(q = value) } },
                )
            }
        }
    }
}

@Composable
private fun ValueSlider(
    label: String,
    value: Double,
    minimum: Double,
    maximum: Double,
    step: Double,
    suffix: String,
    onValue: (Double) -> Unit,
    logarithmic: Boolean = false,
    enabled: Boolean = true,
) {
    var editing by rememberSaveable { mutableStateOf(false) }
    val haptics = rememberPumperHaptics()
    val sliderHaptics = rememberSliderHaptics()
    val sliderValue = if (logarithmic) {
        (log10(value.coerceIn(minimum, maximum) / minimum) / log10(maximum / minimum)).toFloat()
    } else {
        value.coerceIn(minimum, maximum).toFloat()
    }
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            TextButton(
                onClick = {
                    haptics.selection()
                    editing = true
                },
                enabled = enabled,
                shapes = ButtonDefaults.shapes(),
            ) {
                Text(valueLabel(value, suffix))
            }
        }
        Slider(
            value = sliderValue,
            onValueChange = { raw ->
                val hapticFraction = if (logarithmic) {
                    raw
                } else {
                    ((raw - minimum.toFloat()) / (maximum - minimum).toFloat())
                }
                sliderHaptics.update(hapticFraction)
                val next = if (logarithmic) minimum * (maximum / minimum).pow(raw.toDouble()) else raw.toDouble()
                onValue((round(next / step) * step).coerceIn(minimum, maximum))
            },
            onValueChangeFinished = sliderHaptics::finish,
            valueRange = if (logarithmic) 0f..1f else minimum.toFloat()..maximum.toFloat(),
            enabled = enabled,
        )
    }
    if (editing) {
        NumberEntryDialog(
            title = label,
            value = value,
            minimum = minimum,
            maximum = maximum,
            suffix = suffix,
            onDismiss = { editing = false },
            onValue = {
                onValue(it)
                editing = false
            },
        )
    }
}

@Composable
private fun NumberEntryDialog(
    title: String,
    value: Double,
    minimum: Double,
    maximum: Double,
    suffix: String,
    onDismiss: () -> Unit,
    onValue: (Double) -> Unit,
) {
    var text by remember(value) { mutableStateOf(formatNumber(value)) }
    val parsed = text.toDoubleOrNull()
    val valid = parsed != null && parsed in minimum..maximum
    val focusManager = LocalFocusManager.current
    val focusRequester = remember { FocusRequester() }
    val haptics = rememberPumperHaptics()
    fun commit() {
        if (valid) {
            haptics.confirm()
            onValue(requireNotNull(parsed))
        }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                singleLine = true,
                isError = text.isNotEmpty() && !valid,
                suffix = if (suffix.isNotEmpty()) ({ Text(suffix) }) else null,
                supportingText = if (!valid) ({ Text("Enter a value from ${formatNumber(minimum)} to ${formatNumber(maximum)}") }) else null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    commit()
                    focusManager.clearFocus()
                }),
                textStyle = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium),
                modifier = Modifier.fillMaxWidth().focusRequester(focusRequester),
            )
        },
        confirmButton = {
            Button(onClick = ::commit, enabled = valid, shapes = ButtonDefaults.shapes()) { Text("Apply") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, shapes = ButtonDefaults.shapes()) { Text("Cancel") }
        },
    )
}

private val NumberFormat = DecimalFormat("0.##", DecimalFormatSymbols(Locale.US))
private fun formatNumber(value: Double): String = synchronized(NumberFormat) { NumberFormat.format(value) }
private fun valueLabel(value: Double, suffix: String): String =
    if (suffix.isEmpty()) formatNumber(value) else "${formatNumber(value)} $suffix"

private fun formatSigned(value: Double): String = if (value > 0) "+${formatNumber(value)}" else formatNumber(value)

private fun formatFrequency(value: Double): String = when {
    value >= 1_000.0 -> "${formatNumber(value / 1_000.0)} kHz"
    else -> "${formatNumber(value)} Hz"
}

internal fun filterOptionsForFirmware(supportsFirmware3Controls: Boolean): List<FilterType> = if (supportsFirmware3Controls) {
    listOf(
        FilterType.Peaking,
        FilterType.LowPass,
        FilterType.LowShelf,
        FilterType.HighPass,
        FilterType.HighShelf,
        FilterType.Notch,
        FilterType.BandPass,
    )
} else {
    listOf(FilterType.Peaking, FilterType.LowShelf, FilterType.HighShelf)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun FilterTypeDropdown(
    selected: FilterType,
    supportsFirmware3Controls: Boolean,
    onSelect: (FilterType) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val options = filterOptionsForFirmware(supportsFirmware3Controls)
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = filterLabel(selected),
            onValueChange = {},
            readOnly = true,
            label = { Text("Filter") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.fillMaxWidth().menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { type ->
                DropdownMenuItem(
                    text = { Text(filterLabel(type)) },
                    onClick = {
                        expanded = false
                        onSelect(type)
                    },
                )
            }
        }
    }
}

private fun FilterType.isGainIndependent(): Boolean = when (this) {
    FilterType.LowPass, FilterType.HighPass, FilterType.Notch, FilterType.BandPass -> true
    else -> false
}

internal fun filterLabel(type: FilterType): String = when (type) {
    FilterType.Peaking -> "Peaking"
    FilterType.LowPass -> "Low-pass"
    FilterType.LowShelf -> "Low shelf"
    FilterType.HighPass -> "High-pass"
    FilterType.HighShelf -> "High shelf"
    FilterType.Notch -> "Notch"
    FilterType.BandPass -> "Band-pass"
}
