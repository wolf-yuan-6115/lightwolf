package dev.lightwolf.pumper.controller.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.ChevronRight
import androidx.compose.material.icons.outlined.DarkMode
import androidx.compose.material.icons.outlined.LightMode
import androidx.compose.material.icons.outlined.PowerSettingsNew
import androidx.compose.material.icons.outlined.RestartAlt
import androidx.compose.material.icons.outlined.Restore
import androidx.compose.material.icons.outlined.SettingsBrightness
import androidx.compose.material.icons.outlined.SystemUpdateAlt
import androidx.compose.material.icons.outlined.Usb
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.BuildConfig
import dev.lightwolf.pumper.controller.ControllerUiState
import dev.lightwolf.pumper.controller.PumperControllerViewModel
import dev.lightwolf.pumper.controller.settings.ThemeMode
import java.text.NumberFormat

private data class InfoValue(val label: String, val value: String)

private data class InfoAction(
    val icon: ImageVector,
    val label: String,
    val supporting: String,
    val onClick: () -> Unit,
    val enabled: Boolean,
)

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun InfoScreen(
    state: ControllerUiState,
    controller: PumperControllerViewModel,
    themeMode: ThemeMode,
    onThemeMode: (ThemeMode) -> Unit,
) {
    val haptics = rememberPumperHaptics()
    val status = state.status
    val number = NumberFormat.getIntegerInstance()
    val audioValues = listOf(
        InfoValue("Sample rate", status?.sampleRateHz?.let { "${it / 1_000.0} kHz" } ?: "-"),
        InfoValue("Active configuration", status?.appliedGeneration?.let(number::format) ?: "-"),
        InfoValue("Worst DSP block", status?.maxDspBlockUs?.let { "${number.format(it)} us" } ?: "-"),
        InfoValue("I2S low-water", status?.i2sLowWaterFrames?.let { "${number.format(it)} frames" } ?: "-"),
        InfoValue("Audio underruns", status?.underrunFrames?.let(number::format) ?: "-"),
        InfoValue("Backpressure events", status?.backpressureEvents?.let(number::format) ?: "-"),
    )
    val hardwareValues = listOf(
        InfoValue("Chip temperature", status?.temperatureC?.let { "%.1f C".format(it) } ?: "-"),
        InfoValue("System clock", status?.systemClockMHz?.let { "%.0f MHz".format(it) } ?: "-"),
    )
    val actions = listOf(
        InfoAction(
            icon = Icons.Outlined.Restore,
            label = "Load factory EQ",
            supporting = "Replace live preview without writing flash",
            onClick = controller::requestRestoreDefaults,
            enabled = !state.deviceOperationBusy,
        ),
        InfoAction(
            icon = Icons.Outlined.RestartAlt,
            label = "Restart",
            supporting = if (status?.supportsDeviceReset == true) {
                "Restart Pumper firmware"
            } else {
                "Requires firmware 1.7 or newer"
            },
            onClick = controller::requestRestart,
            enabled = !state.deviceOperationBusy && status?.supportsDeviceReset == true,
        ),
        InfoAction(
            icon = Icons.Outlined.SystemUpdateAlt,
            label = "Upgrade firmware",
            supporting = "Expose the RP2350 drive for upgrading firmware",
            onClick = controller::requestBootsel,
            enabled = !state.deviceOperationBusy,
        ),
    )

    BoxWithConstraints(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        val contentPadding = if (maxWidth >= 600.dp) PumperSpacing.large else PumperSpacing.medium
        LazyColumn(
            modifier = Modifier.widthIn(max = PumperInfoMaxContentWidth).fillMaxSize(),
            contentPadding = PaddingValues(contentPadding),
            verticalArrangement = Arrangement.spacedBy(PumperSpacing.extraSmall),
        ) {
            item { SectionLabel("Connection", first = true) }
            item {
                InfoSurface {
                    ListItem(
                        modifier = Modifier.fillMaxWidth(),
                        colors = transparentListItemColors(),
                        supportingContent = {
                            Text(
                                "Firmware ${status?.firmwareVersion ?: "-"} / " +
                                    if (status?.streaming == true) "audio active" else "audio idle",
                            )
                        },
                        leadingContent = { Icon(Icons.Outlined.Usb, contentDescription = null) },
                        trailingContent = {
                            FilledTonalIconButton(
                                onClick = {
                                    haptics.confirm()
                                    controller.disconnect()
                                },
                                shapes = IconButtonDefaults.shapes(),
                            ) {
                                Icon(Icons.Outlined.PowerSettingsNew, contentDescription = "Disconnect")
                            }
                        },
                    ) {
                        Text(
                            state.productName ?: "Pumper USB DAC",
                            style = MaterialTheme.typography.bodyLargeEmphasized,
                        )
                    }
                }
            }

            item { SectionLabel("Levels") }
            item {
                InfoSurface {
                    SignalLevels(
                        levels = controller.meter,
                        modifier = Modifier.padding(PumperSpacing.medium),
                    )
                }
            }

            item { SectionLabel("Audio") }
            item(key = "audio-values") {
                ValueGroup(audioValues)
            }

            item { SectionLabel("Hardware") }
            item(key = "hardware-values") {
                ValueGroup(hardwareValues)
            }

            item { SectionLabel("Appearance") }
            item {
                InfoSurface {
                    Column(
                        Modifier.fillMaxWidth().padding(PumperSpacing.medium),
                        verticalArrangement = Arrangement.spacedBy(PumperSpacing.medium),
                    ) {
                        Text("Theme", style = MaterialTheme.typography.bodyLargeEmphasized)
                        ThemeModeControl(themeMode, onThemeMode)
                    }
                }
            }

            item { SectionLabel("Actions") }
            item(key = "actions") {
                InfoSurface {
                    Column(Modifier.fillMaxWidth()) {
                        actions.forEachIndexed { index, action ->
                            ActionRow(action, index, actions.size)
                            if (index < actions.lastIndex) InfoDivider()
                        }
                    }
                }
            }

            item { SectionLabel("App") }
            item {
                InfoSurface {
                    ListItem(
                        modifier = Modifier.fillMaxWidth(),
                        colors = transparentListItemColors(),
                        trailingContent = {
                            Text(
                                BuildConfig.VERSION_NAME,
                                style = MaterialTheme.typography.bodyLargeEmphasized,
                                color = MaterialTheme.colorScheme.primary,
                            )
                        },
                    ) {
                        Text("Version")
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun SectionLabel(label: String, first: Boolean = false) {
    Text(
        label,
        style = MaterialTheme.typography.titleMediumEmphasized,
        color = MaterialTheme.colorScheme.onSurface,
        modifier = Modifier.padding(
            start = PumperSpacing.small,
            end = PumperSpacing.small,
            top = if (first) PumperSpacing.small else PumperSpacing.large,
            bottom = PumperSpacing.small,
        ),
    )
}

@Composable
private fun InfoSurface(content: @Composable () -> Unit) {
    Surface(
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        modifier = Modifier.fillMaxWidth(),
        content = content,
    )
}

@Composable
private fun ValueGroup(values: List<InfoValue>) {
    InfoSurface {
        Column(Modifier.fillMaxWidth()) {
            values.forEachIndexed { index, info ->
                ValueRow(info)
                if (index < values.lastIndex) InfoDivider()
            }
        }
    }
}

@Composable
private fun ValueRow(info: InfoValue) {
    ListItem(
        modifier = Modifier.fillMaxWidth(),
        colors = transparentListItemColors(),
        trailingContent = {
            Text(
                info.value,
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary,
            )
        },
    ) {
        Text(info.label)
    }
}

@Composable
private fun InfoDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(horizontal = PumperSpacing.medium),
        color = MaterialTheme.colorScheme.outlineVariant,
    )
}

@Composable
private fun transparentListItemColors() = ListItemDefaults.colors(
    containerColor = Color.Transparent,
)

@Composable
private fun ThemeModeControl(themeMode: ThemeMode, onThemeMode: (ThemeMode) -> Unit) {
    val haptics = rememberPumperHaptics()
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        val options = listOf(
            Triple(ThemeMode.System, "System", Icons.Outlined.SettingsBrightness),
            Triple(ThemeMode.Light, "Light", Icons.Outlined.LightMode),
            Triple(ThemeMode.Dark, "Dark", Icons.Outlined.DarkMode),
        )
        options.forEachIndexed { index, (mode, label, icon) ->
            SegmentedButton(
                selected = themeMode == mode,
                onClick = {
                    if (themeMode != mode) {
                        haptics.selection()
                        onThemeMode(mode)
                    }
                },
                shape = SegmentedButtonDefaults.itemShape(index, options.size),
                icon = { Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp)) },
                label = { Text(label) },
            )
        }
    }
}

@Composable
private fun ActionRow(action: InfoAction, index: Int, count: Int) {
    val haptics = rememberPumperHaptics()
    SegmentedListItem(
        onClick = {
            haptics.selection()
            action.onClick()
        },
        enabled = action.enabled,
        shapes = ListItemDefaults.segmentedShapes(index, count),
        colors = ListItemDefaults.segmentedColors(
            containerColor = Color.Transparent,
            disabledContainerColor = Color.Transparent,
        ),
        modifier = Modifier.fillMaxWidth(),
        supportingContent = { Text(action.supporting) },
        leadingContent = { Icon(action.icon, contentDescription = null) },
        trailingContent = { Icon(Icons.Outlined.ChevronRight, contentDescription = null) },
    ) {
        Text(action.label)
    }
}
