package dev.lightwolf.pumper.controller

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.lightwolf.pumper.controller.protocol.DefaultEqConfig
import dev.lightwolf.pumper.controller.protocol.CrossfeedConfig
import dev.lightwolf.pumper.controller.protocol.CrossfeedState
import dev.lightwolf.pumper.controller.protocol.EqBand
import dev.lightwolf.pumper.controller.protocol.EqConfig
import dev.lightwolf.pumper.controller.protocol.EqMath
import dev.lightwolf.pumper.controller.protocol.EqValidation
import dev.lightwolf.pumper.controller.protocol.FilterType
import dev.lightwolf.pumper.controller.protocol.METER_HEARTBEAT_INTERVAL_MS
import dev.lightwolf.pumper.controller.protocol.METER_REPORT_INTERVAL_MS
import dev.lightwolf.pumper.controller.protocol.METER_TIMEOUT_MS
import dev.lightwolf.pumper.controller.protocol.MeterLevel
import dev.lightwolf.pumper.controller.protocol.Opcode
import dev.lightwolf.pumper.controller.protocol.OutputProcessingConfig
import dev.lightwolf.pumper.controller.protocol.OutputProcessingState
import dev.lightwolf.pumper.controller.protocol.PumperProtocol
import dev.lightwolf.pumper.controller.protocol.WidthMode
import dev.lightwolf.pumper.controller.transport.PumperClient
import dev.lightwolf.pumper.controller.transport.PumperDevice
import dev.lightwolf.pumper.controller.transport.PumperTransport
import dev.lightwolf.pumper.controller.transport.UsbDeviceManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.max
import kotlin.math.min

private const val LIVE_PREVIEW_INTERVAL_MS = 55L

private fun AppDestination.usesMeter(): Boolean = this == AppDestination.Info

class PumperControllerViewModel(application: Application) : AndroidViewModel(application) {
    private val deviceManager: UsbDeviceManager = (application as PumperApplication).deviceManager
    private val _state = MutableStateFlow(ControllerUiState())
    val state = _state.asStateFlow()
    private val _meter = MutableStateFlow<MeterLevel?>(null)
    val meter = _meter.asStateFlow()

    private var client: PumperClient? = null
    private var sessionScope: CoroutineScope? = null
    private var savedConfig: EqConfig? = null
    private var pendingGlobal: EqConfig? = null
    private val pendingBands = mutableMapOf<Int, EqBand>()
    private var previewJob: Job? = null
    private var pendingCrossfeed: CrossfeedConfig? = null
    private var crossfeedPreviewJob: Job? = null
    private var crossfeedRevision = 0L
    private var pendingOutputProcessing: OutputProcessingConfig? = null
    private var outputProcessingPreviewJob: Job? = null
    private var outputProcessingRevision = 0L
    private var sessionRevision = 0L
    private var submittedPreampDb = DefaultEqConfig.preampDb
    private var foreground = false

    fun onForeground(intent: Intent?) {
        foreground = true
        refreshDevices()
        if (_state.value.connection != ConnectionState.Disconnected) return
        val intentDevice = deviceManager.deviceFromIntent(intent)
        val target = intentDevice ?: deviceManager.attachedDevices().firstOrNull()
        if (target != null) connect(target)
    }

    fun onBackground() {
        foreground = false
        disconnect()
    }

    fun refreshDevices() {
        _state.update { it.copy(availableDevices = deviceManager.attachedDevices()) }
    }

    fun connect(device: PumperDevice? = _state.value.availableDevices.firstOrNull()) {
        if (device == null) {
            refreshDevices()
            _state.update { it.copy(error = "Connect Pumper to this Android device with a USB data cable.") }
            return
        }
        connectWith { deviceManager.open(device) }
    }

    fun connectSimulator() {
        val transport = DebugFeatures.createTransport()
        if (transport == null) {
            _state.update { it.copy(error = "The simulated DAC is only available in debug builds.") }
            return
        }
        connectWith { transport }
    }

    private fun connectWith(openTransport: suspend () -> PumperTransport) {
        if (_state.value.connection != ConnectionState.Disconnected) return
        viewModelScope.launch {
            _state.update { it.copy(connection = ConnectionState.Connecting, error = null, message = null) }
            try {
                val transport = openTransport()
                if (!foreground) {
                    transport.close()
                    _state.update { it.copy(connection = ConnectionState.Disconnected) }
                    return@launch
                }
                val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
                sessionScope = scope
                val nextClient = PumperClient(transport, scope)
                client = nextClient
                _state.update {
                    it.copy(connection = ConnectionState.Connected, productName = nextClient.productName)
                }
                sessionRevision++
                readDevice(knownStoredProfile = false)
                startSessionTasks(scope, nextClient)
            } catch (error: Throwable) {
                closeSession()
                _state.update {
                    it.copy(
                        connection = ConnectionState.Disconnected,
                        productName = null,
                        error = error.message ?: "Unable to connect to Pumper",
                    )
                }
            }
        }
    }

    fun disconnect() {
        if (_state.value.connection == ConnectionState.Disconnected) return
        viewModelScope.launch {
            runCatching { client?.request(Opcode.MeterStop, timeoutMs = 300) }
            closeSession()
            _state.update {
                it.copy(
                    connection = ConnectionState.Disconnected,
                    productName = null,
                    status = null,
                    audioControls = null,
                    crossfeed = null,
                    crossfeedSaving = false,
                    outputProcessing = null,
                    outputProcessingSaving = false,
                    busy = false,
                    confirmation = null,
                )
            }
        }
    }

    fun setDestination(destination: AppDestination) {
        _state.update { it.copy(destination = destination) }
        if (!destination.usesMeter()) _meter.value = null
    }

    fun selectBand(index: Int) {
        if (index in _state.value.config.bands.indices) _state.update { it.copy(selectedBand = index) }
    }

    fun updateBand(index: Int, transform: (EqBand) -> EqBand) {
        val current = _state.value.config
        val oldBand = current.bands.getOrNull(index) ?: return
        var newBand = transform(oldBand)
        val shelf = newBand.type == FilterType.LowShelf || newBand.type == FilterType.HighShelf
        if (shelf) newBand = newBand.copy(q = newBand.q.coerceIn(0.1, 1.0))
        if (newBand.type.value >= FilterType.LowPass.value) newBand = newBand.copy(widthMode = WidthMode.Q)
        EqValidation.band(newBand, index)?.let {
            _state.update { state -> state.copy(error = it) }
            return
        }
        val bands = current.bands.toMutableList().also { it[index] = newBand }
        var next = current.copy(bands = bands)
        var globalChanged = false
        if (_state.value.autoPreamp) {
            next = next.copy(preampDb = EqMath.calculateAutoPreamp(next, sampleRate()).preampDb)
            globalChanged = next.preampDb != current.preampDb
        }
        commitConfig(next)
        pendingBands[index] = newBand
        if (globalChanged) pendingGlobal = next
        schedulePreview()
    }

    fun updateGlobal(enabled: Boolean? = null, preampDb: Double? = null) {
        val current = _state.value.config
        var next = current.copy(
            enabled = enabled ?: current.enabled,
            preampDb = preampDb ?: current.preampDb,
        )
        if (_state.value.autoPreamp) next = next.copy(preampDb = EqMath.calculateAutoPreamp(next, sampleRate()).preampDb)
        EqValidation.config(next)?.let {
            _state.update { state -> state.copy(error = it) }
            return
        }
        commitConfig(next)
        pendingGlobal = next
        schedulePreview()
    }

    fun setAutoPreamp(enabled: Boolean) {
        _state.update { it.copy(autoPreamp = enabled) }
        if (enabled) {
            val next = _state.value.config.copy(
                preampDb = EqMath.calculateAutoPreamp(_state.value.config, sampleRate()).preampDb,
            )
            commitConfig(next)
            pendingGlobal = next
            schedulePreview()
        }
    }

    fun selectProfile(index: Int) {
        val current = _state.value
        if (index !in 0 until current.profiles.count || index == current.selectedProfile) return
        if (!current.profiles.isPresent(index)) {
            _state.update {
                it.copy(selectedProfile = index, message = "Profile ${index + 1} selected as a save target.")
            }
        } else if (current.hasUnsavedEdits) {
            _state.update { it.copy(confirmation = Confirmation.SwitchProfile(index)) }
        } else {
            runBusy { loadProfile(index) }
        }
    }

    fun requestSaveProfile() = requestConfirmation(Confirmation.SaveProfile)
    fun makeProfileDefault(index: Int) {
        val current = _state.value
        if (index !in 0 until current.profiles.count ||
            !current.profiles.isPresent(index) ||
            current.profiles.persistedProfile == index
        ) return
        runBusy { setDefaultProfile(index) }
    }

    fun deleteProfile(index: Int) {
        val current = _state.value
        if (index !in 0 until current.profiles.count || !current.profiles.isPresent(index)) return
        runBusy { deleteStoredProfile(index) }
    }

    fun requestRestoreDefaults() = requestConfirmation(Confirmation.RestoreDefaults)
    fun requestRestart() = requestConfirmation(Confirmation.Restart)
    fun requestBootsel() = requestConfirmation(Confirmation.Bootsel)

    fun dismissConfirmation() {
        _state.update { it.copy(confirmation = null) }
    }

    fun confirmAction() {
        val action = _state.value.confirmation ?: return
        _state.update { it.copy(confirmation = null) }
        runBusy {
            when (action) {
                Confirmation.SaveProfile -> saveProfile()
                Confirmation.RestoreDefaults -> restoreDefaults()
                is Confirmation.SwitchProfile -> loadProfile(action.index)
                Confirmation.Restart -> resetDevice(Opcode.RestartDevice, "Pumper is restarting.")
                Confirmation.Bootsel -> resetDevice(Opcode.EnterBootsel, "Pumper entered BOOTSEL. Copy a UF2 to the RP2350 USB drive.")
            }
        }
    }

    fun clearMessage() {
        _state.update { it.copy(message = null) }
    }

    fun clearError() {
        _state.update { it.copy(error = null) }
    }

    fun updateCrossfeed(config: CrossfeedConfig) {
        val current = _state.value
        if (!current.connected || current.crossfeedSaving || current.status?.supportsAudioControls != true) return
        val verified = runCatching {
            PumperProtocol.decodeCrossfeed(PumperProtocol.encodeCrossfeed(config))
        }.getOrElse { error ->
            _state.update { it.copy(error = error.message ?: "Invalid crossfeed settings.") }
            return
        }
        val state = current.crossfeed ?: return
        crossfeedRevision++
        pendingCrossfeed = verified
        _state.update {
            it.copy(
                crossfeed = state.copy(live = verified, dirty = verified != state.saved),
                message = null,
                error = null,
            )
        }
        scheduleCrossfeedPreview()
    }

    fun saveCrossfeed() {
        val current = _state.value
        if (!current.connected || current.crossfeedSaving || current.crossfeed?.dirty != true) return
        viewModelScope.launch {
            _state.update { it.copy(crossfeedSaving = true, error = null, message = null) }
            try {
                crossfeedPreviewJob?.cancel()
                crossfeedPreviewJob = null
                pendingCrossfeed = _state.value.crossfeed?.live
                flushCrossfeedPreview(throwOnFailure = true)
                val activeClient = requireNotNull(client)
                val verified = PumperProtocol.decodeCrossfeedState(
                    activeClient.request(Opcode.SaveCrossfeed, timeoutMs = 8_000).payload,
                )
                crossfeedRevision++
                _state.update { it.copy(crossfeed = verified, message = "Crossfeed saved for power-on.") }
            } catch (error: Throwable) {
                rereadCrossfeed()
                _state.update { it.copy(error = error.message ?: "The DAC could not save crossfeed.") }
            } finally {
                _state.update { it.copy(crossfeedSaving = false) }
            }
        }
    }

    fun updateOutputProcessing(config: OutputProcessingConfig) {
        val current = _state.value
        if (!current.connected || current.outputProcessingSaving || current.status?.supportsFirmware3Controls != true) return
        val verified = runCatching {
            PumperProtocol.decodeOutputProcessing(PumperProtocol.encodeOutputProcessing(config))
        }.getOrElse { error ->
            _state.update { it.copy(error = error.message ?: "Invalid output processing settings.") }
            return
        }
        val state = current.outputProcessing ?: return
        outputProcessingRevision++
        pendingOutputProcessing = verified
        _state.update {
            it.copy(
                outputProcessing = state.copy(live = verified, dirty = verified != state.saved),
                message = null,
                error = null,
            )
        }
        scheduleOutputProcessingPreview()
    }

    fun saveOutputProcessing() {
        val current = _state.value
        if (!current.connected || current.outputProcessingSaving || current.outputProcessing?.dirty != true) return
        viewModelScope.launch {
            val activeSession = sessionRevision
            _state.update { it.copy(outputProcessingSaving = true, error = null, message = null) }
            try {
                outputProcessingPreviewJob?.cancel()
                outputProcessingPreviewJob = null
                pendingOutputProcessing = _state.value.outputProcessing?.live
                flushOutputProcessingPreview(throwOnFailure = true)
                val verified = PumperProtocol.decodeOutputProcessingState(
                    requireNotNull(client).request(Opcode.SaveOutputProcessing, timeoutMs = 8_000).payload,
                )
                if (activeSession != sessionRevision) return@launch
                outputProcessingRevision++
                _state.update { it.copy(outputProcessing = verified, message = "Output processing saved for power-on.") }
            } catch (error: Throwable) {
                if (activeSession == sessionRevision) {
                    rereadOutputProcessing()
                    _state.update { it.copy(error = error.message ?: "The DAC could not save output processing.") }
                }
            } finally {
                if (activeSession == sessionRevision) _state.update { it.copy(outputProcessingSaving = false) }
            }
        }
    }

    private fun requestConfirmation(confirmation: Confirmation) {
        if (!_state.value.connected || _state.value.deviceOperationBusy) return
        _state.update { it.copy(confirmation = confirmation) }
    }

    private fun runBusy(block: suspend () -> Unit) {
        if (!_state.value.connected || _state.value.deviceOperationBusy) return
        viewModelScope.launch {
            _state.update { it.copy(busy = true, error = null, message = null) }
            try {
                block()
            } catch (error: Throwable) {
                _state.update { it.copy(error = error.message ?: "The operation failed.") }
            } finally {
                _state.update { it.copy(busy = false) }
            }
        }
    }

    private suspend fun readDevice(knownStoredProfile: Boolean) {
        val activeClient = client ?: return
        val nextStatus = PumperProtocol.decodeStatus(activeClient.request(Opcode.Hello).payload)
        val nextAudio = if (nextStatus.supportsAudioControls && _state.value.audioControls == null) {
            PumperProtocol.decodeAudioControls(activeClient.request(Opcode.GetAudioControls).payload)
        } else _state.value.audioControls
        val nextCrossfeed = if (nextStatus.supportsAudioControls && _state.value.crossfeed == null) {
            PumperProtocol.decodeCrossfeedState(activeClient.request(Opcode.GetCrossfeed).payload)
        } else _state.value.crossfeed
        val nextOutputProcessing = if (nextStatus.supportsFirmware3Controls && _state.value.outputProcessing == null) {
            PumperProtocol.decodeOutputProcessingState(activeClient.request(Opcode.GetOutputProcessing).payload)
        } else _state.value.outputProcessing
        val (enabled, preampDb) = PumperProtocol.decodeGlobal(activeClient.request(Opcode.GetGlobal).payload)
        val nextProfiles = PumperProtocol.decodeProfileState(activeClient.request(Opcode.GetProfiles).payload)
        val bands = List(nextStatus.bandCount) { index ->
            PumperProtocol.decodeBand(activeClient.request(Opcode.GetBand, byteArrayOf(index.toByte())).payload).second
        }
        val deviceConfig = EqConfig(enabled, preampDb, bands)
        var next = deviceConfig
        if (_state.value.autoPreamp) {
            next = next.copy(preampDb = EqMath.calculateAutoPreamp(next, nextStatus.sampleRateHz).preampDb)
            if (next.preampDb != deviceConfig.preampDb) {
                activeClient.request(Opcode.SetGlobal, PumperProtocol.encodeGlobal(next))
            }
        }
        if (knownStoredProfile || !nextStatus.dirty) savedConfig = deviceConfig
        submittedPreampDb = next.preampDb
        EqValidation.config(next)?.let { throw IllegalStateException("The DAC returned an invalid configuration. $it") }
        _state.update {
            it.copy(
                config = next,
                status = nextStatus,
                audioControls = nextAudio,
                crossfeed = nextCrossfeed,
                outputProcessing = nextOutputProcessing,
                profiles = nextProfiles,
                selectedProfile = nextProfiles.activeProfile,
                selectedBand = min(it.selectedBand, max(0, bands.lastIndex)),
                hasUnsavedEdits = savedConfig == null || next != savedConfig,
                error = null,
            )
        }
    }

    private fun startSessionTasks(scope: CoroutineScope, activeClient: PumperClient) {
        scope.launch {
            activeClient.meterLevels.catch { }.collect { level -> _meter.value = level }
        }
        scope.launch {
            activeClient.disconnects.collect {
                closeSession()
                _state.update {
                    it.copy(
                        connection = ConnectionState.Disconnected,
                        productName = null,
                        status = null,
                        audioControls = null,
                        crossfeed = null,
                        crossfeedSaving = false,
                        outputProcessing = null,
                        outputProcessingSaving = false,
                        busy = false,
                        error = "Pumper disconnected.",
                    )
                }
            }
        }
        scope.launch {
            state.map { it.destination.usesMeter() }
                .distinctUntilChanged()
                .collect { enabled ->
                    if (enabled) {
                        activeClient.request(
                            Opcode.MeterStart,
                            PumperProtocol.encodeMeterConfig(METER_REPORT_INTERVAL_MS, METER_TIMEOUT_MS),
                        )
                        while (isActive && _state.value.destination.usesMeter()) {
                            delay(METER_HEARTBEAT_INTERVAL_MS)
                            runCatching { activeClient.request(Opcode.MeterKeepalive) }
                        }
                    } else {
                        _meter.value = null
                        runCatching { activeClient.request(Opcode.MeterStop, timeoutMs = 300) }
                    }
                }
        }
        scope.launch {
            while (isActive) {
                delay(1_000)
                runCatching { PumperProtocol.decodeStatus(activeClient.request(Opcode.GetStatus).payload) }
                    .onSuccess { status ->
                        val oldStatus = _state.value.status
                        val oldRate = _state.value.status?.sampleRateHz
                        if (oldStatus != null &&
                            (oldStatus.firmwareMajor != status.firmwareMajor || oldStatus.firmwareMinor != status.firmwareMinor) &&
                            !status.supportsFirmware3Controls
                        ) {
                            clearPendingOutputProcessing()
                            _state.update { it.copy(outputProcessing = null, outputProcessingSaving = false) }
                        }
                        _state.update { it.copy(status = status) }
                        if (_state.value.autoPreamp && oldRate != status.sampleRateHz) {
                            val next = _state.value.config.copy(
                                preampDb = EqMath.calculateAutoPreamp(_state.value.config, status.sampleRateHz).preampDb,
                            )
                            commitConfig(next)
                            pendingGlobal = next
                            schedulePreview()
                        }
                        if (status.supportsAudioControls) refreshAudioState(activeClient)
                    }
            }
        }
    }

    private fun commitConfig(next: EqConfig) {
        _state.update {
            it.copy(
                config = next,
                hasUnsavedEdits = savedConfig == null || next != savedConfig,
                message = null,
                error = null,
            )
        }
    }

    private fun schedulePreview() {
        if (!_state.value.connected || previewJob?.isActive == true) return
        previewJob = viewModelScope.launch {
            delay(LIVE_PREVIEW_INTERVAL_MS)
            flushPreview()
        }
    }

    private suspend fun flushPreview() {
        val activeClient = client ?: return
        val global = pendingGlobal
        val bands = pendingBands.toSortedMap()
        pendingGlobal = null
        pendingBands.clear()
        try {
            val attenuating = global != null && global.preampDb < submittedPreampDb
            if (attenuating) activeClient.request(Opcode.SetGlobal, PumperProtocol.encodeGlobal(global))
            bands.forEach { (index, band) ->
                activeClient.request(Opcode.SetBand, PumperProtocol.encodeBand(index, band))
            }
            if (global != null && !attenuating) {
                activeClient.request(Opcode.SetGlobal, PumperProtocol.encodeGlobal(global))
            }
            if (global != null) submittedPreampDb = global.preampDb
        } catch (error: Throwable) {
            _state.update { it.copy(error = error.message ?: "The DAC rejected the live EQ update.") }
        }
        if (pendingGlobal != null || pendingBands.isNotEmpty()) schedulePreview()
    }

    private fun clearPendingPreview() {
        previewJob?.cancel()
        previewJob = null
        pendingGlobal = null
        pendingBands.clear()
    }

    private fun scheduleCrossfeedPreview() {
        if (!_state.value.connected || crossfeedPreviewJob?.isActive == true) return
        crossfeedPreviewJob = viewModelScope.launch {
            delay(LIVE_PREVIEW_INTERVAL_MS)
            crossfeedPreviewJob = null
            flushCrossfeedPreview()
        }
    }

    private fun scheduleOutputProcessingPreview() {
        if (!_state.value.connected || outputProcessingPreviewJob?.isActive == true) return
        outputProcessingPreviewJob = viewModelScope.launch {
            delay(LIVE_PREVIEW_INTERVAL_MS)
            outputProcessingPreviewJob = null
            flushOutputProcessingPreview()
        }
    }

    private suspend fun flushOutputProcessingPreview(throwOnFailure: Boolean = false): Boolean {
        val activeClient = client ?: return false
        val config = pendingOutputProcessing ?: return true
        pendingOutputProcessing = null
        val editRevision = outputProcessingRevision
        val activeSession = sessionRevision
        try {
            val verified = PumperProtocol.decodeOutputProcessingState(
                activeClient.request(Opcode.SetOutputProcessing, PumperProtocol.encodeOutputProcessing(config)).payload,
            )
            if (activeSession == sessionRevision && editRevision == outputProcessingRevision) {
                _state.update { it.copy(outputProcessing = verified) }
            }
            return true
        } catch (error: Throwable) {
            if (!throwOnFailure && activeSession == sessionRevision && editRevision == outputProcessingRevision) {
                rereadOutputProcessing(editRevision)
                _state.update { it.copy(error = error.message ?: "The DAC rejected the output processing preview.") }
            }
            if (throwOnFailure) throw error
            return false
        } finally {
            if (pendingOutputProcessing != null) scheduleOutputProcessingPreview()
        }
    }

    private suspend fun flushCrossfeedPreview(throwOnFailure: Boolean = false): Boolean {
        val activeClient = client ?: return false
        val config = pendingCrossfeed ?: return true
        pendingCrossfeed = null
        val editRevision = crossfeedRevision
        val activeSession = sessionRevision
        try {
            val verified = PumperProtocol.decodeCrossfeedState(
                activeClient.request(Opcode.SetCrossfeed, PumperProtocol.encodeCrossfeed(config)).payload,
            )
            if (activeSession == sessionRevision && editRevision == crossfeedRevision) {
                _state.update { it.copy(crossfeed = verified) }
            }
            return true
        } catch (error: Throwable) {
            if (!throwOnFailure && activeSession == sessionRevision && editRevision == crossfeedRevision) {
                rereadCrossfeed()
                _state.update { it.copy(error = error.message ?: "The DAC rejected the crossfeed preview.") }
            }
            if (throwOnFailure) throw error
            return false
        } finally {
            if (pendingCrossfeed != null) scheduleCrossfeedPreview()
        }
    }

    private suspend fun refreshAudioState(activeClient: PumperClient) {
        val editRevision = crossfeedRevision
        val outputEditRevision = outputProcessingRevision
        runCatching {
            PumperProtocol.decodeAudioControls(activeClient.request(Opcode.GetAudioControls).payload)
        }.onSuccess { audio ->
            _state.update { it.copy(audioControls = audio) }
        }
        if (pendingCrossfeed == null && !_state.value.crossfeedSaving) {
            runCatching {
                PumperProtocol.decodeCrossfeedState(activeClient.request(Opcode.GetCrossfeed).payload)
            }.onSuccess { crossfeed ->
                if (editRevision == crossfeedRevision && pendingCrossfeed == null) {
                    _state.update { it.copy(crossfeed = crossfeed) }
                }
            }
        }
        if (_state.value.status?.supportsFirmware3Controls == true &&
            pendingOutputProcessing == null && !_state.value.outputProcessingSaving
        ) {
            runCatching {
                PumperProtocol.decodeOutputProcessingState(activeClient.request(Opcode.GetOutputProcessing).payload)
            }.onSuccess { output ->
                if (outputEditRevision == outputProcessingRevision && pendingOutputProcessing == null) {
                    _state.update { it.copy(outputProcessing = output) }
                }
            }
        }
    }

    private suspend fun rereadCrossfeed() {
        val activeClient = client ?: return
        val verified = runCatching {
            PumperProtocol.decodeCrossfeedState(activeClient.request(Opcode.GetCrossfeed).payload)
        }.getOrNull() ?: return
        crossfeedRevision++
        pendingCrossfeed = null
        _state.update { it.copy(crossfeed = verified) }
    }

    private fun clearPendingCrossfeed() {
        crossfeedPreviewJob?.cancel()
        crossfeedPreviewJob = null
        pendingCrossfeed = null
        crossfeedRevision++
        sessionRevision++
    }

    private suspend fun rereadOutputProcessing(expectedRevision: Long? = null) {
        val activeClient = client ?: return
        val activeSession = sessionRevision
        val verified = runCatching {
            PumperProtocol.decodeOutputProcessingState(activeClient.request(Opcode.GetOutputProcessing).payload)
        }.getOrNull() ?: return
        if (activeSession != sessionRevision ||
            (expectedRevision != null && expectedRevision != outputProcessingRevision)
        ) return
        outputProcessingRevision++
        pendingOutputProcessing = null
        _state.update { it.copy(outputProcessing = verified) }
    }

    private fun clearPendingOutputProcessing() {
        outputProcessingPreviewJob?.cancel()
        outputProcessingPreviewJob = null
        pendingOutputProcessing = null
        outputProcessingRevision++
        sessionRevision++
    }

    private suspend fun saveProfile() {
        val activeClient = requireNotNull(client)
        val current = _state.value.config
        EqValidation.config(current)?.let { throw IllegalArgumentException(it) }
        clearPendingPreview()
        activeClient.request(Opcode.SetGlobal, PumperProtocol.encodeGlobal(current))
        current.bands.forEachIndexed { index, band ->
            activeClient.request(Opcode.SetBand, PumperProtocol.encodeBand(index, band))
        }
        activeClient.request(Opcode.SaveProfile, byteArrayOf(_state.value.selectedProfile.toByte()), timeoutMs = 8_000)
        readDevice(knownStoredProfile = true)
        _state.update { it.copy(message = "Profile ${it.selectedProfile + 1} saved to flash.") }
    }

    private suspend fun loadProfile(index: Int) {
        clearPendingPreview()
        requireNotNull(client).request(Opcode.LoadProfile, byteArrayOf(index.toByte()))
        readDevice(knownStoredProfile = true)
        _state.update { it.copy(message = "Profile ${index + 1} loaded into live preview.") }
    }

    private suspend fun setDefaultProfile(index: Int) {
        val activeClient = requireNotNull(client)
        activeClient.request(Opcode.SetDefaultProfile, byteArrayOf(index.toByte()), timeoutMs = 8_000)
        val profiles = PumperProtocol.decodeProfileState(activeClient.request(Opcode.GetProfiles).payload)
        _state.update { it.copy(profiles = profiles, message = "Profile ${index + 1} will load at power-on.") }
    }

    private suspend fun deleteStoredProfile(index: Int) {
        val activeClient = requireNotNull(client)
        activeClient.request(Opcode.DeleteProfile, byteArrayOf(index.toByte()), timeoutMs = 8_000)
        val profiles = PumperProtocol.decodeProfileState(activeClient.request(Opcode.GetProfiles).payload)
        _state.update {
            it.copy(profiles = profiles, message = "Profile ${index + 1} cleared.")
        }
    }

    private suspend fun restoreDefaults() {
        clearPendingPreview()
        requireNotNull(client).request(Opcode.RestoreDefaults)
        savedConfig = null
        _meter.value = null
        readDevice(knownStoredProfile = false)
        _state.update { it.copy(message = "Factory EQ loaded into live preview.") }
    }

    private suspend fun resetDevice(opcode: Opcode, message: String) {
        clearPendingPreview()
        clearPendingCrossfeed()
        clearPendingOutputProcessing()
        requireNotNull(client).request(opcode)
        _state.update { it.copy(message = message) }
    }

    private fun sampleRate(): Long = _state.value.status?.sampleRateHz ?: 48_000L

    private suspend fun closeSession() {
        clearPendingPreview()
        clearPendingCrossfeed()
        clearPendingOutputProcessing()
        val activeClient = client
        client = null
        val activeScope = sessionScope
        sessionScope = null
        runCatching { activeClient?.close() }
        activeScope?.cancel()
        savedConfig = null
    }

    override fun onCleared() {
        sessionScope?.cancel()
        super.onCleared()
    }
}
