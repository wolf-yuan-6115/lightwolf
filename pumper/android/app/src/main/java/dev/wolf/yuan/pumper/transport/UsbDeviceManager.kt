package dev.wolf.yuan.pumper.transport

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import dev.wolf.yuan.pumper.protocol.USB_PRODUCT_ID
import dev.wolf.yuan.pumper.protocol.USB_VENDOR_ID
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

class UsbDeviceManager(private val context: Context) {
    private val usbManager = context.getSystemService(UsbManager::class.java)

    fun attachedDevices(): List<PumperDevice> = usbManager.deviceList.values
        .filter(::matches)
        .sortedBy { it.deviceId }
        .map(::toPumperDevice)

    fun deviceFromIntent(intent: Intent?): PumperDevice? {
        if (intent?.action != UsbManager.ACTION_USB_DEVICE_ATTACHED) return null
        val device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java) ?: return null
        return if (matches(device)) toPumperDevice(device) else null
    }

    suspend fun open(device: PumperDevice): PumperTransport {
        val usbDevice = device.platformDevice as? UsbDevice ?: throw TransportException("Invalid USB device")
        ensurePermission(usbDevice)
        return AndroidHidTransport.open(context, usbManager, usbDevice)
    }

    private suspend fun ensurePermission(device: UsbDevice) {
        if (usbManager.hasPermission(device)) return
        suspendCancellableCoroutine { continuation ->
            val action = "${context.packageName}.USB_PERMISSION.${device.deviceId}"
            val receiver = object : BroadcastReceiver() {
                override fun onReceive(receiverContext: Context, intent: Intent) {
                    if (intent.action != action) return
                    runCatching { context.unregisterReceiver(this) }
                    val returnedDevice = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    if (returnedDevice?.deviceId == device.deviceId && granted) {
                        if (continuation.isActive) continuation.resume(Unit)
                    } else if (continuation.isActive) {
                        continuation.resumeWithException(TransportException("USB permission was not granted"))
                    }
                }
            }
            context.registerReceiver(receiver, IntentFilter(action), Context.RECEIVER_NOT_EXPORTED)
            continuation.invokeOnCancellation { runCatching { context.unregisterReceiver(receiver) } }
            val permissionIntent = PendingIntent.getBroadcast(
                context,
                device.deviceId,
                Intent(action).setPackage(context.packageName),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
            )
            usbManager.requestPermission(device, permissionIntent)
        }
    }

    private fun toPumperDevice(device: UsbDevice) = PumperDevice(
        id = device.deviceId,
        productName = runCatching { device.productName }.getOrNull() ?: "Pumper USB DAC",
        platformDevice = device,
    )

    private fun matches(device: UsbDevice): Boolean =
        device.vendorId == USB_VENDOR_ID && device.productId == USB_PRODUCT_ID
}

