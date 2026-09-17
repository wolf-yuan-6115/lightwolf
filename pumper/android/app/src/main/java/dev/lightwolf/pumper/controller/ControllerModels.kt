package dev.lightwolf.pumper.controller

import dev.lightwolf.pumper.controller.protocol.DefaultEqConfig
import dev.lightwolf.pumper.controller.protocol.DeviceStatus
import dev.lightwolf.pumper.controller.protocol.EqConfig
import dev.lightwolf.pumper.controller.protocol.ProfileState
import dev.lightwolf.pumper.controller.transport.PumperDevice

enum class ConnectionState { Disconnected, Connecting, Connected }

enum class AppDestination { Equalizer, Info }

sealed interface Confirmation {
    data object SaveProfile : Confirmation
    data object RestoreDefaults : Confirmation
    data class SwitchProfile(val index: Int) : Confirmation
    data object Restart : Confirmation
    data object Bootsel : Confirmation
}

data class ControllerUiState(
    val connection: ConnectionState = ConnectionState.Disconnected,
    val availableDevices: List<PumperDevice> = emptyList(),
    val productName: String? = null,
    val config: EqConfig = DefaultEqConfig,
    val status: DeviceStatus? = null,
    val profiles: ProfileState = ProfileState(10, 0, 0, 0, 0),
    val selectedBand: Int = 0,
    val selectedProfile: Int = 0,
    val autoPreamp: Boolean = false,
    val hasUnsavedEdits: Boolean = false,
    val busy: Boolean = false,
    val message: String? = null,
    val error: String? = null,
    val confirmation: Confirmation? = null,
    val destination: AppDestination = AppDestination.Equalizer,
) {
    val connected: Boolean get() = connection == ConnectionState.Connected
    val selectedProfileEmpty: Boolean get() = !profiles.isPresent(selectedProfile)
    val selectedProfileIsDefault: Boolean get() = !selectedProfileEmpty && profiles.persistedProfile == selectedProfile
    val needsSave: Boolean get() = hasUnsavedEdits || (connected && selectedProfileEmpty)
}
