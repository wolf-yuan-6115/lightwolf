package dev.lightwolf.pumper.controller.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import dev.lightwolf.pumper.controller.ConnectionState
import dev.lightwolf.pumper.controller.ControllerUiState
import dev.lightwolf.pumper.controller.protocol.AudioChannelControl
import dev.lightwolf.pumper.controller.protocol.AudioControls
import dev.lightwolf.pumper.controller.protocol.CrossfeedConfig
import dev.lightwolf.pumper.controller.protocol.CrossfeedMode
import dev.lightwolf.pumper.controller.protocol.CrossfeedState
import dev.lightwolf.pumper.controller.protocol.DeviceStatus
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class AudioScreenComposeTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun olderFirmwareShowsBothCompatibilityNotices() {
        composeRule.setContent {
            MaterialTheme {
                AudioScreen(
                    state = ControllerUiState(
                        connection = ConnectionState.Connected,
                        status = deviceStatus(2, 1),
                    ),
                    onCrossfeedChange = {},
                    onSaveCrossfeed = {},
                )
            }
        }

        assertEquals(2, composeRule.onAllNodesWithText("Requires firmware 2.2").fetchSemanticsNodes().size)
    }

    @Test
    fun audioValuesAreReadOnlyAndOnlyCustomEnablesSliders() {
        composeRule.setContent {
            var state by remember { mutableStateOf(supportedState()) }
            MaterialTheme {
                AudioScreen(
                    state = state,
                    onCrossfeedChange = { config ->
                        val current = requireNotNull(state.crossfeed)
                        state = state.copy(crossfeed = current.copy(live = config, dirty = config != current.saved))
                    },
                    onSaveCrossfeed = {},
                )
            }
        }

        composeRule.onNodeWithText("Master volume").assertExists()
        composeRule.onNodeWithText("Muted · −12 dB").assertExists()
        composeRule.onNodeWithText("Medium").performClick()
        composeRule.onNodeWithTag("crossfeed-strength").assertIsNotEnabled()
        composeRule.onNodeWithText("Custom").performClick()
        composeRule.onNodeWithTag("crossfeed-strength").assertIsEnabled()
        composeRule.onNodeWithText("Unsaved").assertExists()
        composeRule.onNodeWithTag("save-crossfeed").assertIsEnabled()
    }
}

private fun supportedState(): ControllerUiState {
    val crossfeed = CrossfeedConfig(CrossfeedMode.Medium, 20.0, 700, 0.25)
    return ControllerUiState(
        connection = ConnectionState.Connected,
        status = deviceStatus(2, 3),
        audioControls = AudioControls(
            master = AudioChannelControl(-12.0, true),
            left = AudioChannelControl(0.0, false),
            right = AudioChannelControl(0.0, false),
        ),
        crossfeed = CrossfeedState(crossfeed, crossfeed, false),
    )
}

private fun deviceStatus(major: Int, minor: Int) = DeviceStatus(
    firmwareMajor = major,
    firmwareMinor = minor,
    bandCount = 10,
    streaming = true,
    dirty = false,
    eqEnabled = true,
    sampleRateHz = 48_000,
    configGeneration = 1,
    savedGeneration = 1,
    appliedGeneration = 1,
    underrunFrames = 0,
    backpressureEvents = 0,
    temperatureC = null,
    systemClockMHz = null,
    maxDspBlockUs = null,
    i2sLowWaterFrames = null,
)
