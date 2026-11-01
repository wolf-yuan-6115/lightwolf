package dev.wolf.yuan.pumper.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.ContentCopy
import androidx.compose.material.icons.outlined.ContentPaste
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import dev.wolf.yuan.pumper.ControllerUiState
import dev.wolf.yuan.pumper.PumperControllerViewModel
import dev.wolf.yuan.pumper.protocol.EqText

@Composable
internal fun EqTextActions(state: ControllerUiState, controller: PumperControllerViewModel) {
    val context = LocalContext.current
    var importing by rememberSaveable { mutableStateOf(false) }
    var text by rememberSaveable { mutableStateOf("") }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    Row(horizontalArrangement = Arrangement.spacedBy(PumperSpacing.small)) {
        TextButton(enabled = state.connected, onClick = {
            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            clipboard.setPrimaryClip(ClipData.newPlainText("Pumper EQ", EqText.serialize(state.config)))
        }) {
            Icon(Icons.Outlined.ContentCopy, contentDescription = null)
            Text("Copy EQ")
        }
        TextButton(enabled = state.connected && !state.deviceOperationBusy, onClick = {
            text = ""
            error = null
            importing = true
        }) {
            Icon(Icons.Outlined.ContentPaste, contentDescription = null)
            Text("Import EQ")
        }
    }
    if (importing) AlertDialog(
        onDismissRequest = { importing = false },
        title = { Text("Import parametric EQ") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(PumperSpacing.small)) {
                Text("Paste EQ text to replace the live preview. Save the profile to keep it on the DAC.")
                OutlinedTextField(
                    value = text, onValueChange = { text = it; error = null },
                    label = { Text("EQ text") }, minLines = 5, maxLines = 10,
                    isError = error != null,
                    supportingText = error?.let { message -> { Text(message) } },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            Button(enabled = text.isNotBlank() && state.connected && !state.deviceOperationBusy, onClick = {
                error = controller.importEqText(text)
                if (error == null) importing = false
            }) { Text("Import") }
        },
        dismissButton = { TextButton(onClick = { importing = false }) { Text("Cancel") } },
    )
}
