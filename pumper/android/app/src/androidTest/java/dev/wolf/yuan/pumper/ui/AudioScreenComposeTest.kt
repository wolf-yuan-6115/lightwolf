package dev.wolf.yuan.pumper.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import dev.wolf.yuan.pumper.ConnectionState
import dev.wolf.yuan.pumper.ControllerUiState
import dev.wolf.yuan.pumper.protocol.AudioChannelControl
import dev.wolf.yuan.pumper.protocol.AudioControls
import dev.wolf.yuan.pumper.protocol.CrossfeedConfig
import dev.wolf.yuan.pumper.protocol.CrossfeedMode
import dev.wolf.yuan.pumper.protocol.CrossfeedState
import dev.wolf.yuan.pumper.protocol.DeviceStatus
import dev.wolf.yuan.pumper.protocol.DefaultOutputProcessingConfig
import dev.wolf.yuan.pumper.protocol.OutputProcessingState
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
                    onOutputProcessingChange = {},
                    onSaveOutputProcessing = {},
                )
            }
        }

        assertEquals(2, composeRule.onAllNodesWithText("Requires firmware 2.2").fetchSemanticsNodes().size)
        assertEquals(1, composeRule.onAllNodesWithText("Requires firmware 3.0").fetchSemanticsNodes().size)
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
                    onOutputProcessingChange = {},
                    onSaveOutputProcessing = {},
                )
            }
        }

        composeRule.onNodeWithText("Master volume").assertExists()
        composeRule.onNodeWithText("Bit depth").assertExists()
        composeRule.onNodeWithText("16-bit").assertExists()
        composeRule.onNodeWithText("Muted · −12 dB").assertExists()
        composeRule.onNodeWithText("Medium").performClick()
        composeRule.onNodeWithTag("crossfeed-strength").assertIsNotEnabled()
        composeRule.onNodeWithText("Custom").performClick()
        composeRule.onNodeWithTag("crossfeed-strength").assertIsEnabled()
        composeRule.onNodeWithText("Unsaved").assertExists()
        composeRule.onNodeWithTag("save-crossfeed").assertIsEnabled()
    }

    @Test
    fun legacyStatusOmitsBitDepth() {
        composeRule.setContent {
            MaterialTheme {
                AudioScreen(
                    state = supportedState(bitDepth = null),
                    onCrossfeedChange = {},
                    onSaveCrossfeed = {},
                    onOutputProcessingChange = {},
                    onSaveOutputProcessing = {},
                )
            }
        }

        composeRule.onNodeWithText("Bit depth").assertDoesNotExist()
    }

    @Test
    fun firmware3OutputControlsAreIndependentAndMonoPreservesWidth() {
        composeRule.setContent {
            var state by remember { mutableStateOf(supportedState(3, 0)) }
            MaterialTheme {
                AudioScreen(
                    state = state,
                    onCrossfeedChange = {},
                    onSaveCrossfeed = {},
                    onOutputProcessingChange = { config ->
                        val current = requireNotNull(state.outputProcessing)
                        state = state.copy(outputProcessing = current.copy(live = config, dirty = config != current.saved))
                    },
                    onSaveOutputProcessing = {},
                )
            }
        }

        composeRule.onNodeWithTag("output-processing-card").assertExists()
        composeRule.onNodeWithTag("output-width").assertIsEnabled()
        composeRule.onNodeWithTag("output-mono").performClick()
        composeRule.onNodeWithTag("output-width").assertIsNotEnabled()
        composeRule.onNodeWithTag("save-output-processing").assertIsEnabled()
        composeRule.onNodeWithText("Unsaved").assertExists()
    }
}

private fun supportedState(major: Int = 2, minor: Int = 3, bitDepth: Int? = 16): ControllerUiState {
    val crossfeed = CrossfeedConfig(CrossfeedMode.Medium, 20.0, 700, 0.25)
    return ControllerUiState(
        connection = ConnectionState.Connected,
        status = deviceStatus(major, minor, bitDepth),
        audioControls = AudioControls(
            master = AudioChannelControl(-12.0, true),
            left = AudioChannelControl(0.0, false),
            right = AudioChannelControl(0.0, false),
        ),
        crossfeed = CrossfeedState(crossfeed, crossfeed, false),
        outputProcessing = if (major >= 3) {
            OutputProcessingState(DefaultOutputProcessingConfig, DefaultOutputProcessingConfig, false)
        } else null,
    )
}

private fun deviceStatus(major: Int, minor: Int, bitDepth: Int? = null) = DeviceStatus(
    firmwareMajor = major,
    firmwareMinor = minor,
    bandCount = 10,
    streaming = true,
    dirty = false,
    eqEnabled = true,
    sampleRateHz = 48_000,
    bitDepth = bitDepth,
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
