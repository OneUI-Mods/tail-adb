package net.ryuya_dev.tailadb

/** Preserve pre-migration text behavior using Kotlin whitespace and Regex operations. */
internal object TextCompatibility {
    private fun isWhitespace(char: Char): Boolean =
        char.isWhitespace() && char != '\u00a0' && char != '\u2007' && char != '\u202f'

    fun strip(value: String): String = value.trim { isWhitespace(it) }

    fun isBlank(value: CharSequence): Boolean = value.all { isWhitespace(it) }

    fun split(value: String, regex: String): Array<String> {
        // Compile even for empty input so malformed expressions still fail as before.
        val separator = Regex(regex)
        if (value.isEmpty()) return arrayOf("")
        val parts = separator.split(value)
        val first = separator.find(value)
        val start = if (first != null && first.range.first == 0 && first.value.isEmpty()) 1 else 0
        var end = parts.size
        while (end > start && parts[end - 1].isEmpty()) end--
        return parts.subList(start, end).toTypedArray()
    }
}
