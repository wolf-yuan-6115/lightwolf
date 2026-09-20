package dev.wolf.yuan.pumper.transport

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.hardware.usb.UsbRequest
import dev.wolf.yuan.pumper.protocol.REPORT_SIZE
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer
import java.util.concurrent.TimeoutException
import java.util.concurrent.atomic.AtomicBoolean

internal class AndroidHidTransport private constructor(
    private val context: Context,
    private val device: UsbDevice,
    private val connection: UsbDeviceConnection,
    private val hidInterface: UsbInterface,
    private val inputEndpoint: UsbEndpoint,
    private val outputEndpoint: UsbEndpoint,
) : PumperTransport {
    private data class Outgoing(val bytes: ByteArray, val completion: CompletableDeferred<Unit>)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val outgoing = Channel<Outgoing>(Channel.UNLIMITED)
    private val _reports = MutableSharedFlow<ByteArray>(extraBufferCapacity = 32)
    private val _disconnects = MutableSharedFlow<Unit>(extraBufferCapacity = 1)
    private val closed = AtomicBoolean(false)
    private val ioJob: Job

    override val productName: String = runCatching { device.productName }.getOrNull() ?: "Pumper USB DAC"
    override val reports: Flow<ByteArray> = _reports.asSharedFlow()
    override val disconnects: Flow<Unit> = _disconnects.asSharedFlow()

    private val detachReceiver = object : BroadcastReceiver() {
        override fun onReceive(receiverContext: Context, intent: Intent) {
            if (intent.action != UsbManager.ACTION_USB_DEVICE_DETACHED) return
            val detached = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            if (detached?.deviceId != device.deviceId) return
            _disconnects.tryEmit(Unit)
            scope.launch { close() }
        }
    }

    init {
        context.registerReceiver(detachReceiver, IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED), Context.RECEIVER_EXPORTED)
        ioJob = scope.launch { ioLoop() }
    }

    override suspend fun sendReport(report: ByteArray) {
        if (report.size != REPORT_SIZE) throw TransportException("HID reports must be $REPORT_SIZE bytes")
        if (closed.get()) throw TransportException("Pumper is not connected")
        val completion = CompletableDeferred<Unit>()
        outgoing.send(Outgoing(report.copyOf(), completion))
        completion.await()
    }

    override suspend fun close() {
        if (!closed.compareAndSet(false, true)) return
        outgoing.close()
        ioJob.cancel()
        runCatching { context.unregisterReceiver(detachReceiver) }
        withContext(Dispatchers.IO) {
            runCatching { connection.releaseInterface(hidInterface) }
            connection.close()
        }
    }

    private suspend fun ioLoop() {
        val inputRequest = UsbRequest()
        val outputRequest = UsbRequest()
        val inputBuffer = ByteBuffer.allocateDirect(REPORT_SIZE)
        check(inputRequest.initialize(connection, inputEndpoint)) { "Unable to initialize HID input" }
        check(outputRequest.initialize(connection, outputEndpoint)) { "Unable to initialize HID output" }
        check(inputRequest.queue(inputBuffer)) { "Unable to queue HID input" }

        var activeWrite: Outgoing? = null
        var outputBuffer: ByteBuffer? = null
        try {
            while (scope.isActive && !closed.get()) {
                if (activeWrite == null) {
                    outgoing.tryReceive().getOrNull()?.let { next ->
                        outputBuffer = ByteBuffer.allocateDirect(REPORT_SIZE).apply {
                            put(next.bytes)
                            flip()
                        }
                        if (!outputRequest.queue(outputBuffer)) {
                            next.completion.completeExceptionally(TransportException("Unable to queue HID output"))
                        } else {
                            activeWrite = next
                        }
                    }
                }

                val completed = try {
                    connection.requestWait(50)
                } catch (_: TimeoutException) {
                    null
                }
                when (completed) {
                    inputRequest -> {
                        val length = inputBuffer.position()
                        inputBuffer.flip()
                        if (length == REPORT_SIZE) {
                            val bytes = ByteArray(REPORT_SIZE)
                            inputBuffer.get(bytes)
                            _reports.emit(bytes)
                        }
                        inputBuffer.clear()
                        if (!closed.get() && !inputRequest.queue(inputBuffer)) {
                            throw TransportException("Unable to requeue HID input")
                        }
                    }
                    outputRequest -> {
                        activeWrite?.completion?.complete(Unit)
                        activeWrite = null
                        outputBuffer = null
                    }
                    null -> Unit
                }
            }
        } catch (error: Throwable) {
            if (!closed.get()) {
                activeWrite?.completion?.completeExceptionally(error)
                _disconnects.tryEmit(Unit)
            }
        } finally {
            activeWrite?.completion?.completeExceptionally(TransportException("Pumper disconnected"))
            inputRequest.cancel()
            outputRequest.cancel()
            inputRequest.close()
            outputRequest.close()
            while (true) {
                val pending = outgoing.tryReceive().getOrNull() ?: break
                pending.completion.completeExceptionally(TransportException("Pumper disconnected"))
            }
        }
    }

    companion object {
        suspend fun open(context: Context, usbManager: UsbManager, device: UsbDevice): AndroidHidTransport =
            withContext(Dispatchers.IO) {
                val hidInterface = (0 until device.interfaceCount)
                    .map(device::getInterface)
                    .firstOrNull { candidate ->
                        candidate.interfaceClass == UsbConstants.USB_CLASS_HID &&
                            candidate.endpoints().any { it.isInterruptIn() && it.maxPacketSize == REPORT_SIZE } &&
                            candidate.endpoints().any { it.isInterruptOut() && it.maxPacketSize == REPORT_SIZE }
                    }
                    ?: throw TransportException("Pumper HID interface was not found")
                val input = hidInterface.endpoints().first { it.isInterruptIn() && it.maxPacketSize == REPORT_SIZE }
                val output = hidInterface.endpoints().first { it.isInterruptOut() && it.maxPacketSize == REPORT_SIZE }
                val connection = usbManager.openDevice(device) ?: throw TransportException("Unable to open Pumper")
                val claimed = connection.claimInterface(hidInterface, false) || connection.claimInterface(hidInterface, true)
                if (!claimed) {
                    connection.close()
                    throw TransportException("Unable to claim Pumper HID interface")
                }
                AndroidHidTransport(context, device, connection, hidInterface, input, output)
            }
    }
}

private fun UsbInterface.endpoints(): List<UsbEndpoint> = (0 until endpointCount).map(::getEndpoint)
private fun UsbEndpoint.isInterruptIn(): Boolean = type == UsbConstants.USB_ENDPOINT_XFER_INT && direction == UsbConstants.USB_DIR_IN
private fun UsbEndpoint.isInterruptOut(): Boolean = type == UsbConstants.USB_ENDPOINT_XFER_INT && direction == UsbConstants.USB_DIR_OUT
