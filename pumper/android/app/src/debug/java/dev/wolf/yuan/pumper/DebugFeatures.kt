package dev.wolf.yuan.pumper

import dev.wolf.yuan.pumper.transport.PumperTransport

object DebugFeatures {
    val available: Boolean = true
    fun createTransport(): PumperTransport? = SimulatedPumperTransport()
}
