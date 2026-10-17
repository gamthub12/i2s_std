package vn.local.alarmspeaker

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.SystemClock
import java.io.File
import java.io.RandomAccessFile
import kotlin.math.abs

/** Foreground-only recorder. The worker owns and always releases AudioRecord. */
class PhoneRecorder {
    @Volatile private var running = false
    @Volatile private var input: AudioRecord? = null
    @Volatile private var workerActive = false

    @SuppressLint("MissingPermission")
    @Synchronized fun start(file: File, progress: (Long, Float) -> Unit, complete: (File?, String?) -> Unit) {
        check(!workerActive)
        workerActive = true
        running = true
        Thread({
            var recorder: AudioRecord? = null
            var result: File? = null
            var failure: String? = null
            try {
                val minimum = AudioRecord.getMinBufferSize(WavFormat.SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
                check(minimum > 0) { "Điện thoại không hỗ trợ thu âm 16 kHz" }
                recorder = AudioRecord(MediaRecorder.AudioSource.MIC, WavFormat.SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, maxOf(minimum, 8192))
                check(recorder.state == AudioRecord.STATE_INITIALIZED) { "Không thể mở micro" }
                input = recorder
                if (!running) throw IllegalStateException("Đã hủy thu âm")
                recorder.startRecording()
                check(recorder.recordingState == AudioRecord.RECORDSTATE_RECORDING) { "Không thể bắt đầu thu âm" }
                var total = 0
                val buffer = ByteArray(4096)
                val started = SystemClock.elapsedRealtime()
                var lastProgress = 0L
                RandomAccessFile(file, "rw").use { out ->
                    out.setLength(0); out.write(WavFormat.header(0))
                    while (running && total < WavFormat.MAX_DATA_BYTES && SystemClock.elapsedRealtime() - started < 30500) {
                        val count = recorder.read(buffer, 0, minOf(buffer.size, WavFormat.MAX_DATA_BYTES - total))
                        if (count <= 0) {
                            if (!running) break
                            error("Micro bị gián đoạn. Hãy thử thu lại.")
                        }
                        val aligned = count and -2
                        out.write(buffer, 0, aligned); total += aligned
                        if (SystemClock.elapsedRealtime() - lastProgress > 80) {
                            var peak = 0
                            for (i in 0 until aligned step 2) {
                                val sample = ((buffer[i].toInt() and 255) or (buffer[i + 1].toInt() shl 8)).toShort().toInt()
                                peak = maxOf(peak, abs(sample))
                            }
                            progress(total * 1000L / WavFormat.BYTES_PER_SECOND, peak / 32768f)
                            lastProgress = SystemClock.elapsedRealtime()
                        }
                    }
                    check(total >= WavFormat.BYTES_PER_SECOND / 2) { "Bản thu quá ngắn. Hãy thu ít nhất 1 giây." }
                    out.seek(0); out.write(WavFormat.header(total)); out.fd.sync()
                }
                result = file
            } catch (error: Exception) {
                file.delete()
                failure = error.message ?: "Không thể thu âm"
            } finally {
                running = false
                input = null
                try { recorder?.stop() } catch (_: Exception) { }
                recorder?.release()
                workerActive = false
            }
            complete(result, failure)
        }, "phone-alarm-recorder").start()
    }

    fun stop() {
        running = false
        try { input?.stop() } catch (_: Exception) { }
    }
}
