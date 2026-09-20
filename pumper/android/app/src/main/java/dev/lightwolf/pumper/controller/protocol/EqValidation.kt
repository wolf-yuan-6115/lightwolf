package dev.lightwolf.pumper.controller.protocol

object EqValidation {
    fun config(config: EqConfig): String? {
        if (config.preampDb !in -241.0..12.0) return "Preamp gain must be between -241 and 12 dB."
        config.bands.forEachIndexed { index, band ->
            band(band, index)?.let { return it }
        }
        return null
    }

    fun band(band: EqBand, index: Int): String? {
        val label = "Band ${index + 1}"
        if (band.frequencyHz !in 20.0..20_000.0) return "$label frequency must be between 20 and 20,000 Hz."
        if (band.gainDb !in -24.0..24.0) return "$label gain must be between -24 and 24 dB."
        if (band.bandwidthOctaves !in 0.1..4.0) return "$label bandwidth must be between 0.1 and 4 octaves."
        if (band.type.value >= FilterType.LowPass.value && band.widthMode != WidthMode.Q) {
            return "$label width mode must be Q."
        }
        val shelf = band.type == FilterType.LowShelf || band.type == FilterType.HighShelf
        val qMaximum = if (shelf) 1.0 else 20.0
        if (band.q !in 0.1..qMaximum) {
            val name = if (shelf) "slope" else "Q"
            return "$label $name must be between 0.1 and $qMaximum."
        }
        return null
    }
}
