package dev.lightwolf.pumper.controller

import dev.lightwolf.pumper.controller.transport.PumperTransport

object DebugFeatures {
    val available: Boolean = true
    fun createTransport(): PumperTransport? = SimulatedPumperTransport()
}
