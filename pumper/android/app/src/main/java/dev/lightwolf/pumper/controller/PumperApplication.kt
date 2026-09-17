package dev.lightwolf.pumper.controller

import android.app.Application
import dev.lightwolf.pumper.controller.settings.AppSettings
import dev.lightwolf.pumper.controller.transport.UsbDeviceManager

class PumperApplication : Application() {
    val deviceManager by lazy { UsbDeviceManager(applicationContext) }
    val settings by lazy { AppSettings(applicationContext) }
}
