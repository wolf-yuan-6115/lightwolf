@file:OptIn(
    androidx.compose.material3.ExperimentalMaterial3Api::class,
    androidx.compose.material3.ExperimentalMaterial3ExpressiveApi::class,
)

package dev.lightwolf.pumper.controller.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Inventory2
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.outlined.AddBox
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.ExpandLess
import androidx.compose.material.icons.outlined.ExpandMore
import androidx.compose.material.icons.outlined.Save
import androidx.compose.material.icons.outlined.StarOutline
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.ControllerUiState
import dev.lightwolf.pumper.controller.PumperControllerViewModel

@Composable
internal fun ProfileSelector(
    state: ControllerUiState,
    controller: PumperControllerViewModel,
    onManage: (Int) -> Unit,
) {
    var expanded by rememberSaveable { mutableStateOf(false) }
    val haptics = rememberPumperHaptics()
    val selected = state.selectedProfile
    val selectedPresent = state.profiles.isPresent(selected)
    val itemCount = state.profiles.count
    val expandedIndexes = (0 until itemCount).filter { it != selected }
    val containerColor = MaterialTheme.colorScheme.surfaceContainerHigh
    val colors = ListItemDefaults.segmentedColors(
        containerColor = containerColor,
        disabledContainerColor = containerColor,
    )

    Column(
        Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(PumperSpacing.extraSmall),
    ) {
        SegmentedListItem(
            selected = true,
            onClick = {
                if (!state.deviceOperationBusy) {
                    haptics.selection()
                    expanded = !expanded
                }
            },
            enabled = !state.deviceOperationBusy,
            onLongClick = if (selectedPresent) ({
                expanded = false
                onManage(selected)
            }) else null,
            onLongClickLabel = if (selectedPresent) "Profile ${selected + 1} actions" else null,
            shapes = ListItemDefaults.segmentedShapes(0, if (expanded) itemCount else 1),
            colors = colors,
            modifier = Modifier.fillMaxWidth(),
            leadingContent = { ProfileNumber(selected, selected = true) },
            trailingContent = {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(PumperSpacing.small),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    ProfileStatusIcons(selected, state)
                    if (state.needsSave) {
                        IconButton(
                            onClick = {
                                haptics.selection()
                                controller.requestSaveProfile()
                            },
                            enabled = !state.deviceOperationBusy,
                            shapes = IconButtonDefaults.shapes(),
                        ) {
                            Icon(Icons.Outlined.Save, contentDescription = "Save profile ${selected + 1}")
                        }
                    }
                    Icon(
                        if (expanded) Icons.Outlined.ExpandLess else Icons.Outlined.ExpandMore,
                        contentDescription = if (expanded) "Collapse profiles" else "Expand profiles",
                    )
                }
            },
        ) {
            Text("Profile ${selected + 1}", style = MaterialTheme.typography.bodyLargeEmphasized)
        }
        AnimatedVisibility(
            visible = expanded,
            enter = expandVertically(expandFrom = Alignment.Top),
            exit = shrinkVertically(shrinkTowards = Alignment.Top),
            modifier = Modifier.fillMaxWidth(),
        ) {
            Column(
                Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(PumperSpacing.extraSmall),
            ) {
                expandedIndexes.forEachIndexed { position, index ->
                    val present = state.profiles.isPresent(index)
                    SegmentedListItem(
                        onClick = {
                            haptics.selection()
                            expanded = false
                            controller.selectProfile(index)
                        },
                        enabled = !state.deviceOperationBusy,
                        onLongClick = if (present) ({
                            expanded = false
                            onManage(index)
                        }) else null,
                        onLongClickLabel = if (present) "Profile ${index + 1} actions" else null,
                        shapes = ListItemDefaults.segmentedShapes(position + 1, itemCount),
                        colors = colors,
                        modifier = Modifier.fillMaxWidth(),
                        leadingContent = { ProfileNumber(index, selected = false) },
                        trailingContent = { ProfileStatusIcons(index, state) },
                    ) {
                        Text("Profile ${index + 1}", style = MaterialTheme.typography.bodyLarge)
                    }
                }
            }
        }
    }
}

@Composable
internal fun ProfileActionsDialog(
    index: Int,
    state: ControllerUiState,
    onDismiss: () -> Unit,
    onMakeDefault: () -> Unit,
    onDelete: () -> Unit,
) {
    if (!state.profiles.isPresent(index)) return
    val haptics = rememberPumperHaptics()
    val isDefault = state.profiles.persistedProfile == index
    val containerColor = MaterialTheme.colorScheme.surfaceContainerHighest
    val standardColors = ListItemDefaults.segmentedColors(
        containerColor = containerColor,
        disabledContainerColor = containerColor,
    )
    val deleteColors = ListItemDefaults.segmentedColors(
        containerColor = containerColor,
        contentColor = MaterialTheme.colorScheme.error,
        leadingContentColor = MaterialTheme.colorScheme.error,
        supportingContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
        disabledContainerColor = containerColor,
    )

    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Filled.Inventory2, contentDescription = null) },
        title = { Text("Profile ${index + 1}") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(PumperSpacing.extraSmall)) {
                SegmentedListItem(
                    onClick = {
                        haptics.confirm()
                        onMakeDefault()
                    },
                    enabled = !state.deviceOperationBusy && !isDefault,
                    shapes = ListItemDefaults.segmentedShapes(0, 2),
                    colors = standardColors,
                    leadingContent = {
                        Icon(
                            if (isDefault) Icons.Filled.Star else Icons.Outlined.StarOutline,
                            contentDescription = null,
                        )
                    },
                    supportingContent = { Text("Loads when Pumper powers on") },
                ) {
                    Text(if (isDefault) "Power-on default" else "Make power-on default")
                }
                SegmentedListItem(
                    onClick = {
                        haptics.confirm()
                        onDelete()
                    },
                    enabled = !state.deviceOperationBusy,
                    shapes = ListItemDefaults.segmentedShapes(1, 2),
                    colors = deleteColors,
                    leadingContent = { Icon(Icons.Outlined.Delete, contentDescription = null) },
                    supportingContent = { Text("Removes the stored EQ from flash") },
                ) {
                    Text("Delete profile")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

@Composable
private fun ProfileNumber(index: Int, selected: Boolean) {
    Box(
        Modifier
            .size(32.dp)
            .background(
                if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHighest,
                CircleShape,
            ),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            "${index + 1}",
            style = if (selected) {
                MaterialTheme.typography.labelLargeEmphasized
            } else {
                MaterialTheme.typography.labelLarge
            },
            color = if (selected) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun ProfileStatusIcons(index: Int, state: ControllerUiState) {
    val present = state.profiles.isPresent(index)
    Row(horizontalArrangement = Arrangement.spacedBy(PumperSpacing.small)) {
        Icon(
            if (present) Icons.Filled.Inventory2 else Icons.Outlined.AddBox,
            contentDescription = if (present) "Stored" else "Empty save slot",
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(20.dp),
        )
        if (present && state.profiles.activeProfile == index) {
            Icon(
                Icons.Filled.CheckCircle,
                contentDescription = "Active",
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(20.dp),
            )
        }
        if (present && state.profiles.persistedProfile == index) {
            Icon(
                Icons.Filled.Star,
                contentDescription = "Power-on default",
                tint = MaterialTheme.colorScheme.tertiary,
                modifier = Modifier.size(20.dp),
            )
        }
    }
}
