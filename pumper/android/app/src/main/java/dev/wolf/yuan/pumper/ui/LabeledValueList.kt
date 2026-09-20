package dev.wolf.yuan.pumper.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color

internal data class LabeledValue(val label: String, val value: String)

@Composable
internal fun SectionHeading(title: String, modifier: Modifier = Modifier) {
    Text(
        title,
        modifier = modifier.padding(horizontal = PumperSpacing.small),
        style = MaterialTheme.typography.titleMediumEmphasized,
        color = MaterialTheme.colorScheme.onSurface,
    )
}

/** A consistent, compact treatment for device status values across the app. */
@Composable
internal fun LabeledValueList(values: List<LabeledValue>, modifier: Modifier = Modifier) {
    Column(modifier.fillMaxWidth()) {
        values.forEachIndexed { index, value ->
            ListItem(
                modifier = Modifier.fillMaxWidth(),
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                trailingContent = {
                    Text(
                        value.value,
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                },
            ) {
                Text(value.label)
            }
            if (index < values.lastIndex) {
                PumperDivider()
            }
        }
    }
}

@Composable
internal fun PumperDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(horizontal = PumperSpacing.medium),
        color = MaterialTheme.colorScheme.outlineVariant,
    )
}
