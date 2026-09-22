package dev.wolf.yuan.pumper.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import dev.wolf.yuan.pumper.protocol.MeterLevel
import dev.wolf.yuan.pumper.protocol.StereoMeterLevel
import kotlinx.coroutines.flow.MutableStateFlow
import org.junit.Rule
import org.junit.Test

class LevelMeterComposeTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun stereoFieldHasACompactHierarchy() {
        composeRule.setContent {
            MaterialTheme {
                SignalLevels(MutableStateFlow<MeterLevel?>(null))
            }
        }

        composeRule.onNodeWithText("Output").assertIsDisplayed()
        composeRule.onNodeWithText("dBFS").assertIsDisplayed()
        composeRule.onNodeWithContentDescription("Limiter inactive").assertIsDisplayed()
        composeRule.onNodeWithContentDescription("Stereo input level").assertIsDisplayed()
        composeRule.onAllNodesWithText("Nested").assertCountEquals(0)
    }

    @Test
    fun limiterIndicatorShowsReportedActivity() {
        val silence = StereoMeterLevel(0, 0, 0, 0)
        composeRule.setContent {
            MaterialTheme {
                SignalLevels(MutableStateFlow(MeterLevel(1, silence, silence, limiterActive = true)))
            }
        }

        composeRule.onNodeWithContentDescription("Limiter active").assertIsDisplayed()
    }
}
