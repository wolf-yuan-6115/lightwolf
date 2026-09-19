package dev.lightwolf.pumper.controller.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Equalizer
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.outlined.Equalizer
import androidx.compose.material.icons.outlined.FolderOpen
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Headphones
import androidx.compose.material.icons.outlined.PowerSettingsNew
import androidx.compose.material.icons.outlined.Save
import androidx.compose.material.icons.outlined.Usb
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearWavyProgressIndicator
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteDefaults
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffold
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffoldValue
import androidx.compose.material3.adaptive.navigationsuite.rememberNavigationSuiteScaffoldState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import dev.lightwolf.pumper.controller.AppDestination
import dev.lightwolf.pumper.controller.Confirmation
import dev.lightwolf.pumper.controller.ConnectionState
import dev.lightwolf.pumper.controller.ControllerUiState
import dev.lightwolf.pumper.controller.DebugFeatures
import dev.lightwolf.pumper.controller.PumperControllerViewModel
import dev.lightwolf.pumper.controller.settings.ThemeMode

private data class DestinationItem(
    val destination: AppDestination,
    val label: String,
    val icon: ImageVector,
    val selectedIcon: ImageVector,
)

private val Destinations = listOf(
    DestinationItem(AppDestination.Equalizer, "EQ", Icons.Outlined.Equalizer, Icons.Filled.Equalizer),
    DestinationItem(AppDestination.Audio, "Audio", Icons.Outlined.Headphones, Icons.Filled.Headphones),
    DestinationItem(AppDestination.Info, "Info", Icons.Outlined.Info, Icons.Filled.Info),
)

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun PumperApp(
    state: ControllerUiState,
    controller: PumperControllerViewModel,
    themeMode: ThemeMode,
    onThemeMode: (ThemeMode) -> Unit,
) {
    val snackbarHost = remember { SnackbarHostState() }
    val haptics = rememberPumperHaptics()
    val navigationSuiteState = rememberNavigationSuiteScaffoldState(
        if (state.connected) {
            NavigationSuiteScaffoldValue.Visible
        } else {
            NavigationSuiteScaffoldValue.Hidden
        },
    )
    LaunchedEffect(state.connected) {
        if (state.connected) {
            navigationSuiteState.show()
        } else {
            navigationSuiteState.hide()
        }
    }
    LaunchedEffect(state.error, state.message) {
        val error = state.error
        val message = state.message
        when {
            error != null -> {
                haptics.reject()
                snackbarHost.showSnackbar(error)
                controller.clearError()
            }
            message != null -> {
                snackbarHost.showSnackbar(message)
                controller.clearMessage()
            }
        }
    }

    NavigationSuiteScaffold(
        navigationSuiteColors = NavigationSuiteDefaults.colors(
            navigationBarContainerColor = MaterialTheme.colorScheme.surface,
            navigationRailContainerColor = MaterialTheme.colorScheme.surface,
            navigationDrawerContainerColor = MaterialTheme.colorScheme.surface,
        ),
        state = navigationSuiteState,
        navigationSuiteItems = {
            Destinations.forEach { destinationItem ->
                val selected = state.destination == destinationItem.destination
                item(
                    selected = selected,
                    onClick = {
                        if (!selected) {
                            haptics.selection()
                            controller.setDestination(destinationItem.destination)
                        }
                    },
                    icon = {
                        Icon(
                            if (selected) destinationItem.selectedIcon else destinationItem.icon,
                            contentDescription = null,
                        )
                    },
                    label = { Text(destinationItem.label) },
                )
            }
        },
    ) {
        Scaffold(
            snackbarHost = { SnackbarHost(snackbarHost) },
        ) { padding ->
            Box(Modifier.fillMaxSize().padding(padding)) {
                if (!state.connected) {
                    ConnectionPane(
                        state = state,
                        onRefresh = controller::refreshDevices,
                        onConnect = { controller.connect() },
                        onSimulator = controller::connectSimulator,
                    )
                } else {
                    val motionScheme = MaterialTheme.motionScheme
                    AnimatedContent(
                        targetState = state.destination,
                        transitionSpec = {
                            val direction = if (targetState.ordinal > initialState.ordinal) 1 else -1
                            val enter = slideInHorizontally(
                                animationSpec = motionScheme.defaultSpatialSpec(),
                                initialOffsetX = { width -> direction * width / 6 },
                            ) + fadeIn(animationSpec = motionScheme.fastEffectsSpec())
                            val exit = slideOutHorizontally(
                                animationSpec = motionScheme.defaultSpatialSpec(),
                                targetOffsetX = { width -> -direction * width / 6 },
                            ) + fadeOut(animationSpec = motionScheme.fastEffectsSpec())
                            enter togetherWith exit
                        },
                        contentKey = { it },
                        label = "Controller destination",
                        modifier = Modifier.fillMaxSize(),
                    ) { destination ->
                        when (destination) {
                            AppDestination.Equalizer -> EqualizerScreen(state, controller)
                            AppDestination.Audio -> AudioScreen(
                                state = state,
                                onCrossfeedChange = controller::updateCrossfeed,
                                onSaveCrossfeed = controller::saveCrossfeed,
                            )
                            AppDestination.Info -> InfoScreen(state, controller, themeMode, onThemeMode)
                        }
                    }
                }
                if (state.busy) {
                    LinearWavyProgressIndicator(Modifier.align(Alignment.TopCenter).fillMaxWidth())
                }
            }
        }
    }
    state.confirmation?.let { confirmation ->
        ConfirmationDialog(confirmation, state, controller::dismissConfirmation, controller::confirmAction)
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun ConnectionPane(
    state: ControllerUiState,
    onRefresh: () -> Unit,
    onConnect: () -> Unit,
    onSimulator: () -> Unit,
) {
    Column(
        Modifier.fillMaxSize().padding(PumperSpacing.large),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Surface(shape = MaterialTheme.shapes.extraExtraLarge, color = MaterialTheme.colorScheme.primaryContainer) {
            Box(Modifier.padding(PumperSpacing.large).size(48.dp), contentAlignment = Alignment.Center) {
                if (state.connection == ConnectionState.Connecting) {
                    LoadingIndicator(color = MaterialTheme.colorScheme.onPrimaryContainer)
                } else {
                    Icon(
                        Icons.Outlined.Usb,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onPrimaryContainer,
                        modifier = Modifier.size(46.dp),
                    )
                }
            }
        }
        Spacer(Modifier.size(PumperSpacing.large))
        Text(
            if (state.connection == ConnectionState.Connecting) "Connecting to Pumper" else "No Pumper connected",
            style = MaterialTheme.typography.headlineSmall,
        )
        Spacer(Modifier.size(PumperSpacing.small))
        Text(
            if (state.availableDevices.isEmpty()) "USB device not detected" else state.availableDevices.first().productName,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.size(PumperSpacing.large))
        if (state.connection != ConnectionState.Connecting) {
            Row(horizontalArrangement = Arrangement.spacedBy(PumperSpacing.small)) {
                TextButton(onClick = onRefresh, shapes = ButtonDefaults.shapes()) { Text("Refresh") }
                Button(
                    onClick = onConnect,
                    shapes = ButtonDefaults.shapes(),
                    enabled = state.availableDevices.isNotEmpty(),
                ) {
                    Icon(Icons.Outlined.Usb, contentDescription = null)
                    Spacer(Modifier.width(PumperSpacing.small))
                    Text("Connect")
                }
            }
            if (DebugFeatures.available) {
                TextButton(
                    onClick = onSimulator,
                    shapes = ButtonDefaults.shapes(),
                    modifier = Modifier.padding(top = PumperSpacing.small),
                ) {
                    Text("Open simulated DAC")
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun ConfirmationDialog(
    confirmation: Confirmation,
    state: ControllerUiState,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    val haptics = rememberPumperHaptics()
    val profile = state.selectedProfile + 1
    val title: String
    val body: String
    val confirmLabel: String
    val icon: ImageVector
    when (confirmation) {
        Confirmation.SaveProfile -> {
            title = "Save profile $profile?"
            body = "The current live EQ will be written to flash in profile $profile."
            confirmLabel = "Save"
            icon = Icons.Outlined.Save
        }
        Confirmation.RestoreDefaults -> {
            title = "Load factory EQ?"
            body = "Factory settings will replace the live preview. Flash remains unchanged until you save a profile."
            confirmLabel = "Load"
            icon = Icons.Outlined.Equalizer
        }
        is Confirmation.SwitchProfile -> {
            title = "Discard live edits?"
            body = "Loading profile ${confirmation.index + 1} will replace the current unsaved EQ edits."
            confirmLabel = "Discard and load"
            icon = Icons.Outlined.FolderOpen
        }
        Confirmation.Restart -> {
            title = "Restart Pumper?"
            body = "Audio and this controller connection will stop while the DAC restarts."
            confirmLabel = "Restart"
            icon = Icons.Outlined.PowerSettingsNew
        }
        Confirmation.Bootsel -> {
            title = "Enter BOOTSEL?"
            body = "Pumper will disconnect and appear as the RP2350 USB drive for a manual UF2 firmware copy."
            confirmLabel = "Enter BOOTSEL"
            icon = Icons.Outlined.Usb
        }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(icon, contentDescription = null) },
        title = { Text(title) },
        text = { Text(body) },
        confirmButton = {
            Button(
                onClick = {
                    haptics.confirm()
                    onConfirm()
                },
                shapes = ButtonDefaults.shapes(),
            ) { Text(confirmLabel) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, shapes = ButtonDefaults.shapes()) { Text("Cancel") }
        },
    )
}
