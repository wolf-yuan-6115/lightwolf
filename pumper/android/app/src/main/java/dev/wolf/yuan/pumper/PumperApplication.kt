package dev.wolf.yuan.pumper

import android.app.Application
import dev.wolf.yuan.pumper.settings.AppSettings
import dev.wolf.yuan.pumper.transport.UsbDeviceManager

class PumperApplication : Application() {
    val deviceManager by lazy { UsbDeviceManager(applicationContext) }
    val settings by lazy { AppSettings(applicationContext) }
}
