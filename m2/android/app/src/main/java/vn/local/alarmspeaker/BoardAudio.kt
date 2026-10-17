package vn.local.alarmspeaker

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

data class SpeakerSound(val filename: String, val bytes: Long)

/** Reuses the existing LAN file API; no paid cloud storage or firmware change. */
class BoardAudio {
    @Volatile private var active: HttpURLConnection? = null
    fun cancel() { active?.disconnect() }

    companion object {
        fun validIp(ip: String) = ip.split('.').let { parts ->
            parts.size == 4 && parts.all { it.isNotEmpty() && it.all(Char::isDigit) && (it.toIntOrNull() ?: -1) in 0..255 }
        }
    }

    private fun connection(ip: String, path: String): HttpURLConnection {
        require(validIp(ip)) { "Nhập đúng địa chỉ IP của loa trên Wi-Fi" }
        return (URL("http://$ip$path").openConnection() as HttpURLConnection).apply {
            connectTimeout = 8000; readTimeout = 30000; instanceFollowRedirects = false
            active = this
        }
    }

    private fun text(ip: String, path: String): String {
        val connection = connection(ip, path)
        try {
            check(connection.responseCode == 200) { "Không đọc được dữ liệu từ loa (HTTP ${connection.responseCode})" }
            return connection.inputStream.bufferedReader(Charsets.UTF_8).use { reader ->
                val chars = CharArray(65537)
                var used = 0
                while (used < chars.size) {
                    val n = reader.read(chars, used, chars.size - used)
                    if (n < 0) break
                    used += n
                }
                check(used < chars.size) { "Danh sách trên loa quá lớn" }
                String(chars, 0, used)
            }
        } finally { connection.disconnect(); active = null }
    }

    fun sounds(ip: String, deviceId: String): List<SpeakerSound> {
        val pairing = JSONObject(text(ip, "/api/pairing"))
        check(pairing.optString("deviceId") == deviceId) { "IP này thuộc một loa khác. Hãy kiểm tra lại." }
        check(JSONObject(text(ip, "/health")).optBoolean("sdMounted")) { "Loa chưa có thẻ SD. Hãy lắp thẻ rồi thử lại." }
        val items = JSONArray(text(ip, "/list"))
        return (0 until items.length()).map { items.getJSONObject(it) }
            .filter { it.optString("name").endsWith(".wav", ignoreCase = true) }
            .map { SpeakerSound(it.getString("name"), it.optLong("size")) }.sortedBy { it.filename }
    }

    suspend fun upload(ip: String, deviceId: String, file: File, progress: (Float) -> Unit): List<SpeakerSound> {
        WavFormat.durationMillis(file)
        require(file.name.matches(Regex("rec_[a-f0-9-]+\\.wav"))) { "Tên bản thu không hợp lệ" }
        val existing = sounds(ip, deviceId)
        check(existing.size < 20 || existing.any { it.filename == file.name }) { "Loa đã có 20 âm báo. Hãy dọn bớt tệp trên thẻ SD." }
        currentCoroutineContext().ensureActive()
        val filename = URLEncoder.encode(file.name, "UTF-8")
        val connection = connection(ip, "/upload?filename=$filename")
        try {
            connection.requestMethod = "POST"; connection.doOutput = true
            connection.setRequestProperty("Content-Type", "audio/wav")
            connection.setFixedLengthStreamingMode(file.length())
            file.inputStream().use { source ->
                connection.outputStream.use { target ->
                    val buffer = ByteArray(8192); var sent = 0L
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        val n = source.read(buffer)
                        if (n < 0) break
                        target.write(buffer, 0, n); sent += n
                        progress(sent.toFloat() / file.length() * 0.8f)
                    }
                }
            }
            check(connection.responseCode == 200) { "Không gửi được bản thu lên thẻ SD (HTTP ${connection.responseCode})" }
        } finally { connection.disconnect(); active = null }
        currentCoroutineContext().ensureActive()
        // Read back the whole small WAV, checking both length and SHA-256 before it can be selected.
        val localHash = MessageDigest.getInstance("SHA-256").apply {
            file.inputStream().use { input -> val b = ByteArray(8192); while (true) { val n = input.read(b); if (n < 0) break; update(b, 0, n) } }
        }.digest()
        val verify = connection(ip, "/download?filename=$filename")
        try {
            check(verify.responseCode == 200) { "Đã gửi nhưng chưa xác minh được tệp trên loa. Hãy thử lại." }
            val digest = MessageDigest.getInstance("SHA-256"); var received = 0L
            verify.inputStream.use { input ->
                val b = ByteArray(8192)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val n = input.read(b); if (n < 0) break
                    received += n
                    check(received <= file.length()) { "Kích thước bản thu trên loa không khớp" }
                    digest.update(b, 0, n); progress(0.8f + received.toFloat() / file.length() * 0.2f)
                }
            }
            check(received == file.length() && MessageDigest.isEqual(localHash, digest.digest())) {
                "Dữ liệu trên thẻ SD không khớp. Hãy gửi lại trước khi dùng làm âm báo."
            }
        } finally { verify.disconnect(); active = null }
        currentCoroutineContext().ensureActive()
        return sounds(ip, deviceId)
    }
}
