package vn.local.alarmspeaker

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Canonical PCM format shared by phone recordings and the ESP32 player. */
object WavFormat {
    const val SAMPLE_RATE = 16000
    const val BYTES_PER_SECOND = SAMPLE_RATE * 2
    const val MAX_SECONDS = 30
    const val MAX_DATA_BYTES = BYTES_PER_SECOND * MAX_SECONDS

    fun header(dataBytes: Int): ByteArray {
        require(dataBytes in 0..MAX_DATA_BYTES && dataBytes % 2 == 0)
        return ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray(Charsets.US_ASCII)); putInt(36 + dataBytes)
            put("WAVEfmt ".toByteArray(Charsets.US_ASCII)); putInt(16)
            putShort(1); putShort(1); putInt(SAMPLE_RATE); putInt(BYTES_PER_SECOND)
            putShort(2); putShort(16); put("data".toByteArray(Charsets.US_ASCII)); putInt(dataBytes)
        }.array()
    }

    fun durationMillis(file: File): Long {
        require(file.length() in 44L..(44L + MAX_DATA_BYTES)) { "Kích thước WAV không hợp lệ" }
        val bytes = ByteArray(44)
        RandomAccessFile(file, "r").use { it.readFully(bytes) }
        val dataBytes = (file.length() - 44).toInt()
        require(bytes.contentEquals(header(dataBytes))) { "Bản thu không phải WAV PCM 16-bit, mono, 16 kHz" }
        return dataBytes * 1000L / BYTES_PER_SECOND
    }
}
