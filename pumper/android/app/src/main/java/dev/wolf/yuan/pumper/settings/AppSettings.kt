package dev.wolf.yuan.pumper.settings

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

enum class ThemeMode { System, Light, Dark }

private val Context.settingsDataStore by preferencesDataStore(name = "controller_settings")

class AppSettings(private val context: Context) {
    private val themeKey = stringPreferencesKey("theme_mode")

    val themeMode: Flow<ThemeMode> = context.settingsDataStore.data.map { preferences ->
        runCatching { ThemeMode.valueOf(preferences[themeKey] ?: ThemeMode.System.name) }
            .getOrDefault(ThemeMode.System)
    }

    suspend fun setThemeMode(mode: ThemeMode) {
        context.settingsDataStore.edit { it[themeKey] = mode.name }
    }
}
