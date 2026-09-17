package dev.lightwolf.pumper.controller

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import dev.lightwolf.pumper.controller.settings.ThemeMode
import dev.lightwolf.pumper.controller.ui.PumperApp
import dev.lightwolf.pumper.controller.ui.theme.PumperTheme
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private val controller by viewModels<PumperControllerViewModel>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val settings = (application as PumperApplication).settings
        setContent {
            val state by controller.state.collectAsStateWithLifecycle()
            val themeMode by settings.themeMode.collectAsStateWithLifecycle(initialValue = ThemeMode.System)
            PumperTheme(themeMode) {
                PumperApp(
                    state = state,
                    controller = controller,
                    themeMode = themeMode,
                    onThemeMode = { mode -> lifecycleScope.launch { settings.setThemeMode(mode) } },
                )
            }
        }
    }

    override fun onStart() {
        super.onStart()
        controller.onForeground(intent)
    }

    override fun onStop() {
        if (!isChangingConfigurations) controller.onBackground()
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        controller.onForeground(intent)
    }
}
