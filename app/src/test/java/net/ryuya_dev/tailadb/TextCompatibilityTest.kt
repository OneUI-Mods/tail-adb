package net.ryuya_dev.tailadb

import org.junit.Assert.*
import org.junit.Test

class TextCompatibilityTest {
    @Test
    fun everyBmpBoundaryRetainsTheJavaStripAndBlankContract() {
        val strip = String::class.java.getMethod("strip")
        val blank = String::class.java.getMethod("isBlank")
        for (code in 0..0xffff) {
            val char = code.toChar()
            val surrounded = "$char-content-$char"
            assertEquals(
                "strip U+${code.toString(16)}",
                strip.invoke(surrounded),
                TextCompatibility.strip(surrounded),
            )
            val single = char.toString()
            assertEquals(
                "blank U+${code.toString(16)}",
                blank.invoke(single),
                TextCompatibility.isBlank(single),
            )
        }
    }

    @Test
    fun nonBreakingSpacesAreContentRatherThanDiscardedRowsOrQueries() {
        for (space in charArrayOf('\u00a0', '\u2007', '\u202f')) {
            assertFalse(TextCompatibility.isBlank(space.toString()))
            assertEquals("$space-title-$space", TextCompatibility.strip("$space-title-$space"))
        }
        assertEquals("title", TextCompatibility.strip("\u2003 title\u2003"))
        assertFalse(TextCompatibility.isBlank("\u0000"))
    }

    @Test
    fun emptySplitRetainsOneElementButDelimiterOnlyInputDoesNot() {
        assertArrayEquals(arrayOf(""), TextCompatibility.split("", ","))
        assertArrayEquals(emptyArray<String>(), TextCompatibility.split(",,,", ","))
        assertArrayEquals(arrayOf("a"), TextCompatibility.split("a,,,", ","))
    }

    @Test
    fun zeroWidthAndPositiveWidthLeadingMatchesFollowJava() {
        assertArrayEquals(arrayOf("a", "b"), TextCompatibility.split("ab", ""))
        assertArrayEquals(arrayOf("", "a"), TextCompatibility.split(",a", ","))
        assertArrayEquals(arrayOf("a", "b"), TextCompatibility.split("ab", "(?=b)"))
    }

    @Test
    fun splitMatchesTheOriginalStringApiAcrossBoundaryInputs() {
        assertThrows(java.util.regex.PatternSyntaxException::class.java) {
            TextCompatibility.split("", "[")
        }
        val original = String::class.java.getMethod("split", String::class.java)
        val inputs =
            arrayOf("", " ", "ab", ",a,,b,,,", "\n", "a\r\nb\n", "\u00a0", "x\u0000y\u0000")
        for (input in inputs) for (regex in arrayOf(",", "\\R", "", "(?=b)", "\u0000", "\\s+")) {
            @Suppress("UNCHECKED_CAST")
            val expected = original.invoke(input, regex) as Array<String>
            assertArrayEquals(
                "input=$input regex=$regex",
                expected,
                TextCompatibility.split(input, regex),
            )
        }
    }
}
