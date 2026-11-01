package dev.wolf.yuan.pumper.protocol

import java.util.Locale

/** The same parametric EQ interchange format as the web controller. */
object EqText {
    private val codes = listOf("LS", "PK", "HS", "LP", "HP", "NO", "BP")
    private val aliases = mapOf("LSC" to "LS", "PEQ" to "PK", "HSC" to "HS", "LPQ" to "LP", "HPQ" to "HP", "NOTCH" to "NO")
    private const val number = "[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)"
    private val filter = Regex("Filter\\s+(\\d+)\\s*:\\s*(ON|OFF)\\s+([A-Z]+)\\s+Fc\\s+($number)\\s*Hz(?:\\s+Gain\\s+($number)\\s*dB)?\\s+(Q|BW)\\s+($number)(?:\\s+oct)?", RegexOption.IGNORE_CASE)
    private fun format(value: Double) = String.format(Locale.US, "%.3f", value).trimEnd('0').trimEnd('.')

    fun serialize(config: EqConfig): String = buildString {
        appendLine("# Pumper Parametric EQ")
        appendLine("Equalizer: ${if (config.enabled) "ON" else "OFF"}")
        appendLine("Preamp: ${format(config.preampDb)} dB")
        config.bands.forEachIndexed { index, band ->
            append("Filter ${index + 1}: ${if (band.enabled) "ON" else "OFF"} ${codes[band.type.value]} Fc ${format(band.frequencyHz)} Hz")
            if (band.type.value <= 2) append(" Gain ${format(band.gainDb)} dB")
            if (band.type == FilterType.Peaking && band.widthMode == WidthMode.Bandwidth) {
                appendLine(" BW ${format(band.bandwidthOctaves)} oct")
            } else appendLine(" Q ${format(band.q)}")
        }
    }

    fun parse(text: String, bandCount: Int = 10): EqConfig {
        require(bandCount in 1..10) { "Unsupported band count." }
        var config = DefaultEqConfig.copy(enabled = true, preampDb = 0.0,
            bands = DefaultEqConfig.bands.take(bandCount).map { it.copy(enabled = false) })
        val seen = mutableSetOf<Int>()
        text.lineSequence().forEachIndexed { lineIndex, source ->
            val line = source.trim()
            if (line.isEmpty() || line.startsWith("#") || line.startsWith(";") || line.equals("Pumper Parametric EQ", true)) return@forEachIndexed
            val equalizer = Regex("Equalizer\\s*:\\s*(ON|OFF)", RegexOption.IGNORE_CASE).matchEntire(line)
            val preamp = Regex("Preamp\\s*:\\s*($number)\\s*dB", RegexOption.IGNORE_CASE).matchEntire(line)
            when {
                equalizer != null -> config = config.copy(enabled = equalizer.groupValues[1].equals("ON", true))
                preamp != null -> config = config.copy(preampDb = preamp.groupValues[1].toDouble())
                else -> {
                    val match = filter.matchEntire(line) ?: error("Line ${lineIndex + 1} is not a supported EQ setting.")
                    val g = match.groupValues
                    val index = (g[1].toIntOrNull() ?: 0) - 1
                    require(index in 0 until bandCount) { "Filter number must be between 1 and $bandCount." }
                    require(seen.add(index)) { "Filter ${index + 1} is listed more than once." }
                    val code = g[3].uppercase(Locale.US)
                    val type = codes.indexOf(aliases[code] ?: code)
                    require(type >= 0) { "Unsupported filter type ($code)." }
                    require(type > 2 || g[5].isNotEmpty()) { "Filter ${index + 1} requires a Gain value." }
                    val widthMode = if (g[6].equals("BW", true)) WidthMode.Bandwidth else WidthMode.Q
                    require(type == 1 || widthMode == WidthMode.Q) { "Filter ${index + 1} only supports Q." }
                    val width = g[7].toDouble()
                    val band = config.bands[index].copy(enabled = g[2].equals("ON", true), type = FilterType.from(type),
                        frequencyHz = g[4].toDouble(), gainDb = g[5].toDoubleOrNull() ?: 0.0, widthMode = widthMode,
                        q = if (widthMode == WidthMode.Q) width else config.bands[index].q,
                        bandwidthOctaves = if (widthMode == WidthMode.Bandwidth) width else config.bands[index].bandwidthOctaves)
                    EqValidation.band(band, index)?.let { error(it) }
                    config = config.copy(bands = config.bands.toMutableList().also { it[index] = band })
                }
            }
        }
        require(seen.isNotEmpty()) { "Paste at least one supported Filter line." }
        EqValidation.config(config)?.let { error(it) }
        return config
    }
}
