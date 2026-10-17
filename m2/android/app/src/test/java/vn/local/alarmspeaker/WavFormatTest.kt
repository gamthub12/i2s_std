package vn.local.alarmspeaker

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class WavFormatTest {
    @Test fun headerMatchesEspPcmContract() {
        val bytes = WavFormat.header(32000)
        val header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("RIFF", String(bytes, 0, 4, Charsets.US_ASCII))
        assertEquals(32036, header.getInt(4))
        assertEquals(1, header.getShort(20).toInt())
        assertEquals(1, header.getShort(22).toInt())
        assertEquals(16000, header.getInt(24))
        assertEquals(32000, header.getInt(28))
        assertEquals(16, header.getShort(34).toInt())
        assertEquals(32000, header.getInt(40))
    }

    @Test fun fullThirtySecondsRemainWithinTheDeviceLimit() {
        val file = File.createTempFile("alarm", ".wav")
        try {
            file.outputStream().use { it.write(WavFormat.header(WavFormat.MAX_DATA_BYTES)); it.write(ByteArray(WavFormat.MAX_DATA_BYTES)) }
            assertEquals(30000L, WavFormat.durationMillis(file))
            assertEquals(960044L, file.length())
        } finally { file.delete() }
    }

    @Test fun rejectsTruncatedOrTamperedWavBeforeUploading() {
        val file = File.createTempFile("alarm", ".wav")
        try {
            file.writeBytes(WavFormat.header(32000) + ByteArray(16))
            assertThrows(IllegalArgumentException::class.java) { WavFormat.durationMillis(file) }
            val header = WavFormat.header(32000); header[22] = 2
            file.writeBytes(header + ByteArray(32000))
            assertThrows(IllegalArgumentException::class.java) { WavFormat.durationMillis(file) }
        } finally { file.delete() }
    }

    @Test fun rejectsUnalignedAndOverlongPcm() {
        assertThrows(IllegalArgumentException::class.java) { WavFormat.header(3) }
        assertThrows(IllegalArgumentException::class.java) { WavFormat.header(WavFormat.MAX_DATA_BYTES + 2) }
    }
}
