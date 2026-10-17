package vn.local.alarmspeaker

import android.app.Application
import android.media.MediaPlayer
import android.os.Handler
import android.os.Looper
import android.util.AtomicFile
import androidx.compose.runtime.*
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.firebase.FirebaseApp
import com.google.firebase.FirebaseOptions
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.auth.FirebaseAuthException
import com.google.firebase.auth.FirebaseUser
import com.google.firebase.database.*
import com.google.firebase.functions.FirebaseFunctions
import kotlinx.coroutines.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.UUID

data class Notice(val id: Long, val text: String)
data class PhoneSound(val label: String, val file: File, val durationMs: Long)
data class AlarmItem(
    val id: Long = 0, val enabled: Boolean = true, val hour: Int = 7, val minute: Int = 0,
    val days: Int = 127, val volume: Int = 80, val ringSeconds: Int = 30,
    val snoozeMinutes: Int = 5, val name: String = "Báo thức", val sound: String = ""
) {
    fun fields(newId: Long = id): Map<String, Any> = mapOf("id" to newId, "enabled" to enabled,
        "hour" to hour, "minute" to minute, "days" to days, "volume" to volume,
        "ringSeconds" to ringSeconds, "snoozeMinutes" to snoozeMinutes, "name" to name, "sound" to sound)
    fun valid() = hour in 0..23 && minute in 0..59 && days in 0..127 && volume in 0..100 &&
        ringSeconds in 5..30 && snoozeMinutes in 0..60 && name.isNotBlank() &&
        name.toByteArray(Charsets.UTF_8).size < 32 && sound.toByteArray(Charsets.UTF_8).size < 96 &&
        (sound.isEmpty() || sound.matches(Regex("[^/\\\\:\\p{Cc}]+\\.wav", RegexOption.IGNORE_CASE)))
    companion object {
        fun from(value: Any?): AlarmItem? {
            val a = value as? Map<*, *> ?: return null
            fun number(key: String, default: Int = 0) = (a[key] as? Number)?.toInt() ?: default
            val id = (a["id"] as? Number)?.toLong() ?: return null
            return AlarmItem(id, a["enabled"] == true, number("hour"), number("minute"), number("days"),
                number("volume", 80), number("ringSeconds", 30), number("snoozeMinutes"),
                a["name"] as? String ?: "Báo thức", a["sound"] as? String ?: "")
        }
    }
}

class AlarmModel(application: Application) : AndroidViewModel(application) {
    companion object {
        // Keep the configured emulator instances when an Activity is closed and reopened.
        private var sharedAuth: FirebaseAuth? = null
        private var sharedDatabase: FirebaseDatabase? = null
        private var sharedFunctions: FirebaseFunctions? = null
    }
    private val prefs = application.getSharedPreferences("alarm_speaker", 0)
    private val main = Handler(Looper.getMainLooper())
    private val auth: FirebaseAuth
    private val database: FirebaseDatabase
    private val functions: FirebaseFunctions
    private val recorder = PhoneRecorder()
    private val board = BoardAudio()
    private var player: MediaPlayer? = null
    private var ioJob: Job? = null
    private var alive = true
    private var recordingGeneration = 0L
    private var loadedUid: String? = null
    private var ownerRef: DatabaseReference? = null
    private var ownerListener: ValueEventListener? = null
    private val deviceListeners = mutableListOf<Pair<DatabaseReference, ValueEventListener>>()
    private val authListener: FirebaseAuth.AuthStateListener
    var user by mutableStateOf<FirebaseUser?>(null); private set
    var authBusy by mutableStateOf(false); private set
    var pairBusy by mutableStateOf(false); private set
    var saveBusy by mutableStateOf(false); private set
    var notice by mutableStateOf<Notice?>(null); private set
    var deviceId by mutableStateOf(""); private set
    var boardIp by mutableStateOf(""); private set
    var ownedDevices by mutableStateOf<List<String>>(emptyList()); private set
    var desired by mutableStateOf<Map<String, Any>>(emptyMap()); private set
    var reported by mutableStateOf<Map<String, Any>>(emptyMap()); private set
    var acks by mutableStateOf<Map<String, Any>>(emptyMap()); private set
    var accessError by mutableStateOf(""); private set
    var speakerSounds by mutableStateOf<List<SpeakerSound>>(emptyList()); private set
    var soundsLoaded by mutableStateOf(false); private set
    var verifiedUploads by mutableStateOf<Set<String>>(emptySet()); private set
    var boardBusy by mutableStateOf(false); private set
    var transferProgress by mutableFloatStateOf(0f); private set
    var transferring by mutableStateOf(""); private set
    var recordings by mutableStateOf<List<PhoneSound>>(emptyList()); private set
    var recording by mutableStateOf(false); private set
    var recordingMs by mutableLongStateOf(0L); private set
    var amplitude by mutableFloatStateOf(0f); private set
    var draft by mutableStateOf<PhoneSound?>(null); private set
    var previewing by mutableStateOf(""); private set
    val ready get() = deviceId.isNotEmpty() && desired["revision"] is Number && accessError.isEmpty()
    val alarms get() = (desired["alarms"] as? Map<*, *>)?.values?.mapNotNull(AlarmItem::from)
        ?.sortedWith(compareBy({ it.hour }, { it.minute }, { it.id })) ?: emptyList()
    val revision get() = (desired["revision"] as? Number)?.toLong() ?: 0L
    val applied get() = (reported["appliedRevision"] as? Number)?.toLong() ?: 0L
    val manualSound get() = desired["manualSound"] as? String ?: ""
    val selectableSounds get() = speakerSounds.filter { remote ->
        val local = recordings.find { it.file.name == remote.filename }
        local == null || remote.filename in verifiedUploads && remote.bytes == local.file.length()
    }
    val playing get() = reported["playing"] == true
    val lastAck get() = acks.values.mapNotNull { it as? Map<*, *> }
        .maxByOrNull { (it["at"] as? Number)?.toLong() ?: 0L }?.get("state") as? String
    fun online(now: Long = System.currentTimeMillis()) =
        (reported["lastSeen"] as? Number)?.toLong()?.let { it > 0 && now - it <= 150000 } ?: false

    init {
        if (FirebaseApp.getApps(application).isEmpty()) FirebaseApp.initializeApp(application,
            FirebaseOptions.Builder().setProjectId(BuildConfig.FIREBASE_PROJECT_ID)
                .setApplicationId(BuildConfig.FIREBASE_APP_ID).setApiKey(BuildConfig.FIREBASE_API_KEY)
                .setDatabaseUrl(BuildConfig.FIREBASE_DATABASE_URL).build())
        auth = sharedAuth ?: FirebaseAuth.getInstance().also {
            if (BuildConfig.USE_FIREBASE_EMULATOR) it.useEmulator(BuildConfig.EMULATOR_HOST, 9099)
            sharedAuth = it
        }
        database = sharedDatabase ?: FirebaseDatabase.getInstance().also {
            if (BuildConfig.USE_FIREBASE_EMULATOR) it.useEmulator(BuildConfig.EMULATOR_HOST, 9000)
            sharedDatabase = it
        }
        functions = sharedFunctions ?: FirebaseFunctions.getInstance("asia-southeast1").also {
            if (BuildConfig.USE_FIREBASE_EMULATOR) it.useEmulator(BuildConfig.EMULATOR_HOST, 5001)
            sharedFunctions = it
        }
        authListener = FirebaseAuth.AuthStateListener { onUser(it.currentUser) }
        auth.addAuthStateListener(authListener)
    }

    fun tell(text: String) { notice = Notice(System.nanoTime(), text) }
    private fun authMessage(error: Exception): String = when ((error as? FirebaseAuthException)?.errorCode) {
        "ERROR_INVALID_EMAIL" -> "Địa chỉ email chưa đúng định dạng."
        "ERROR_EMAIL_ALREADY_IN_USE" -> "Email đã có tài khoản. Hãy chọn Đăng nhập."
        "ERROR_WEAK_PASSWORD" -> "Mật khẩu cần ít nhất 6 ký tự."
        "ERROR_USER_DISABLED" -> "Tài khoản này đã bị vô hiệu hóa."
        "ERROR_TOO_MANY_REQUESTS" -> "Bạn đã thử quá nhiều lần. Hãy đợi một lát."
        "ERROR_WRONG_PASSWORD", "ERROR_USER_NOT_FOUND", "ERROR_INVALID_CREDENTIAL", "ERROR_INVALID_LOGIN_CREDENTIALS" -> "Email hoặc mật khẩu chưa đúng."
        else -> "Không thể đăng nhập. Kiểm tra kết nối mạng và thử lại."
    }

    fun authenticate(email: String, password: String, create: Boolean) {
        if (authBusy) return
        if (!android.util.Patterns.EMAIL_ADDRESS.matcher(email.trim()).matches() || password.length < 6) {
            tell("Nhập email hợp lệ và mật khẩu có ít nhất 6 ký tự."); return
        }
        authBusy = true
        val task = if (create) auth.createUserWithEmailAndPassword(email.trim(), password)
            else auth.signInWithEmailAndPassword(email.trim(), password)
        task.addOnCompleteListener { result ->
            authBusy = false
            if (!result.isSuccessful) tell(authMessage(result.exception ?: Exception()))
        }
    }

    private fun onUser(next: FirebaseUser?) {
        user = next
        if (loadedUid == next?.uid) return
        stopRecording(); stopPreview(); discardDraft(); recordingGeneration++
        recording = false; recordingMs = 0; amplitude = 0f; pairBusy = false; authBusy = false
        ownerListener?.let { ownerRef?.removeEventListener(it) }
        detachDevice(); ownedDevices = emptyList(); loadedUid = next?.uid
        recordings = if (next != null) readRecordings() else emptyList()
        if (next == null) return
        val uid = next.uid
        val saved = prefs.getString("device_$uid", "").orEmpty()
        if (saved.matches(Regex("[0-9a-f]{12}"))) watchDevice(saved, prefs.getString("ip_${uid}_$saved", "").orEmpty())
        ownerRef = database.getReference("users/$uid/devices")
        ownerListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                if (user?.uid != uid) return
                ownedDevices = snapshot.children.mapNotNull { it.key }.filter { it.matches(Regex("[0-9a-f]{12}")) }
                if (deviceId.isEmpty() && ownedDevices.isNotEmpty()) watchDevice(ownedDevices.first(), "")
            }
            override fun onCancelled(error: DatabaseError) { /* Manual device entry also supports Spark. */ }
        }.also { ownerRef!!.addValueEventListener(it) }
    }

    fun logout() {
        stopRecording(); stopPreview(); discardDraft(); cancelBoardOperation()
        auth.signOut(); onUser(null)
    }

    private fun detachDevice() {
        deviceListeners.forEach { (ref, listener) -> ref.removeEventListener(listener) }; deviceListeners.clear()
        cancelBoardOperation(); deviceId = ""; desired = emptyMap(); reported = emptyMap(); acks = emptyMap()
        speakerSounds = emptyList(); verifiedUploads = emptySet(); soundsLoaded = false; boardIp = ""; accessError = ""; saveBusy = false
    }

    fun watchDevice(id: String, ip: String) {
        val uid = user?.uid ?: return
        if (!id.matches(Regex("[0-9a-f]{12}"))) { tell("Device ID cần đúng 12 ký tự từ 0–9 và a–f."); return }
        if (ip.isNotBlank() && !BoardAudio.validIp(ip)) { tell("Địa chỉ IP của loa chưa hợp lệ."); return }
        detachDevice(); deviceId = id; boardIp = ip
        verifiedUploads = prefs.getStringSet("verified_${uid}_$id", emptySet()).orEmpty().toSet()
        prefs.edit().putString("device_$uid", id).putString("ip_${uid}_$id", ip).apply()
        fun listen(path: String, receive: (Map<String, Any>) -> Unit) {
            val ref = database.getReference("devices/$id/$path")
            val listener = object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) {
                    if (user?.uid != uid || deviceId != id) return
                    @Suppress("UNCHECKED_CAST")
                    receive(snapshot.value as? Map<String, Any> ?: emptyMap())
                }
                override fun onCancelled(error: DatabaseError) {
                    if (user?.uid == uid && deviceId == id) {
                        accessError = "Không thể mở loa. Kiểm tra tài khoản sở hữu và Device ID."
                        desired = emptyMap(); reported = emptyMap(); acks = emptyMap(); tell(accessError)
                    }
                }
            }
            deviceListeners += ref to listener; ref.addValueEventListener(listener)
        }
        listen("inbox/desired") { desired = it; if (ready && ip.isNotEmpty() && !soundsLoaded) refreshSounds() }
        listen("reported") { reported = it }
        listen("acks") { acks = it }
    }

    fun connectAudio(ip: String) {
        if (!BoardAudio.validIp(ip)) { tell("Nhập đúng IP của loa, ví dụ 192.168.1.100."); return }
        cancelBoardOperation(); boardIp = ip; speakerSounds = emptyList(); soundsLoaded = false
        prefs.edit().putString("ip_${user?.uid}_$deviceId", ip).apply(); refreshSounds()
    }

    fun pair(id: String, code: String, ip: String) {
        val uid = user?.uid ?: return
        if (pairBusy) return
        if (!id.matches(Regex("[0-9a-f]{12}")) || !code.matches(Regex("[0-9]{6}"))) {
            tell("Kiểm tra Device ID và mã ghép 6 chữ số."); return
        }
        if (ip.isNotBlank() && !BoardAudio.validIp(ip) || BuildConfig.USE_FIREBASE_SPARK && !BoardAudio.validIp(ip)) {
            tell("Nhập IP của loa trên Wi-Fi để ghép."); return
        }
        pairBusy = true
        if (!BuildConfig.USE_FIREBASE_SPARK) {
            functions.getHttpsCallable("claimDevice").call(mapOf("deviceId" to id, "code" to code))
                .addOnSuccessListener { if (user?.uid == uid) { pairBusy = false; watchDevice(id, ip); tell("Đã kết nối với loa của bạn.") } }
                .addOnFailureListener { if (user?.uid == uid) { pairBusy = false; tell("Không ghép được loa. Kiểm tra mã ghép và quyền sở hữu.") } }
        } else {
            auth.currentUser!!.getIdToken(true).addOnSuccessListener { token ->
                viewModelScope.launch {
                    try {
                        withContext(Dispatchers.IO) {
                            val conn = URL("http://$ip/api/pair").openConnection() as HttpURLConnection
                            try {
                                conn.requestMethod = "POST"; conn.connectTimeout = 8000; conn.readTimeout = 30000
                                conn.doOutput = true; conn.setRequestProperty("Content-Type", "application/json")
                                val body = JSONObject().put("deviceId", id).put("code", code).put("idToken", token.token).toString()
                                conn.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                                check(conn.responseCode == 200) { "Không ghép được loa. Kiểm tra mã ghép và mạng Wi-Fi." }
                            } finally { conn.disconnect() }
                        }
                        if (user?.uid == uid) { watchDevice(id, ip); tell("Đã ghép loa thành công.") }
                    } catch (e: Exception) { if (user?.uid == uid) tell(e.message ?: "Không ghép được loa.") }
                    finally { if (user?.uid == uid) pairBusy = false }
                }
            }.addOnFailureListener { if (user?.uid == uid) { pairBusy = false; tell("Phiên đăng nhập hết hạn. Hãy đăng nhập lại.") } }
        }
    }

    private fun changeDesired(change: (MutableMap<String, Any>) -> Boolean, done: () -> Unit = {}) {
        if (!ready || saveBusy) { tell("Chờ tải lịch từ loa rồi thử lại."); return }
        val uid = user?.uid ?: return
        val id = deviceId; saveBusy = true
        database.getReference("devices/$id/inbox/desired").runTransaction(object : Transaction.Handler {
            override fun doTransaction(data: MutableData): Transaction.Result {
                @Suppress("UNCHECKED_CAST")
                val next = (data.value as? Map<String, Any>)?.toMutableMap() ?: return Transaction.abort()
                if (!change(next)) return Transaction.abort()
                next["revision"] = ((next["revision"] as? Number)?.toLong() ?: 0) + 1L
                data.value = next; return Transaction.success(data)
            }
            override fun onComplete(error: DatabaseError?, committed: Boolean, snapshot: DataSnapshot?) {
                if (user?.uid != uid || deviceId != id) return
                saveBusy = false
                if (error != null || !committed) tell("Chưa lưu được thay đổi. Kiểm tra kết nối và giới hạn 20 báo thức.")
                else { tell("Đã lưu lịch, đang đồng bộ tới loa."); done() }
            }
        })
    }

    fun saveAlarm(item: AlarmItem, done: () -> Unit) {
        if (!item.valid()) { tell("Kiểm tra giờ, tên (tối đa 31 byte), âm báo và thời lượng 5–30 giây."); return }
        changeDesired({ next ->
            val all = (next["alarms"] as? Map<*, *>)?.entries?.associate { it.key.toString() to it.value }?.toMutableMap() ?: mutableMapOf()
            if (item.id == 0L && all.size >= 20) return@changeDesired false
            val id = if (item.id != 0L) item.id else maxOf((next["nextId"] as? Number)?.toLong() ?: 1,
                all.values.mapNotNull { AlarmItem.from(it)?.id }.maxOrNull()?.plus(1) ?: 1)
            if (id !in 1..4294967294L || item.id != 0L && !all.containsKey("a_$id")) return@changeDesired false
            all["a_$id"] = item.fields(id); next["alarms"] = all; next["alarmCount"] = all.size
            if (item.id == 0L) next["nextId"] = id + 1
            true
        }, done)
    }

    fun toggleAlarm(item: AlarmItem) = saveAlarm(item.copy(enabled = !item.enabled)) {}
    fun deleteAlarm(item: AlarmItem) = changeDesired({ next ->
        val all = (next["alarms"] as? Map<*, *>)?.entries?.associate { it.key.toString() to it.value }?.toMutableMap() ?: mutableMapOf()
        all.remove("a_${item.id}"); next["alarms"] = all; next["alarmCount"] = all.size; true
    })
    fun setManualSound(filename: String) {
        if (!AlarmItem(sound = filename).valid()) { tell("Tên tệp âm báo không hợp lệ."); return }
        changeDesired({ next -> next["manualSound"] = filename; true })
    }

    fun command(type: String) {
        if (!ready || !online()) { tell("Loa đang offline. Hãy kiểm tra nguồn và kết nối Wi-Fi."); return }
        val uid = user?.uid; val id = deviceId; val now = System.currentTimeMillis()
        database.getReference("devices/$id/inbox/commands").push().setValue(mapOf(
            "type" to type, "issuedAt" to now, "expiresAt" to now + 30000L))
            .addOnSuccessListener { if (user?.uid == uid && deviceId == id) tell("Đã gửi lệnh, đang chờ loa xác nhận.") }
            .addOnFailureListener { if (user?.uid == uid && deviceId == id) tell("Không gửi được lệnh. Hãy thử lại.") }
    }

    private fun cancelBoardOperation() {
        ioJob?.cancel(); board.cancel(); ioJob = null; boardBusy = false; transferring = ""; transferProgress = 0f
    }

    fun refreshSounds() {
        if (boardBusy || !ready) return
        if (!BoardAudio.validIp(boardIp)) { tell("Nhập IP của loa để tải danh sách âm trên thẻ SD."); return }
        val uid = user?.uid; val id = deviceId; val ip = boardIp; boardBusy = true
        ioJob = viewModelScope.launch {
            try {
                val list = withContext(Dispatchers.IO) { board.sounds(ip, id) }
                if (user?.uid == uid && deviceId == id && boardIp == ip) { speakerSounds = list; soundsLoaded = true }
            } catch (_: CancellationException) { }
            catch (e: Exception) { if (user?.uid == uid && deviceId == id) tell("Không tải được âm từ loa. Kiểm tra IP, Wi-Fi và thẻ SD. ${e.message.orEmpty()}") }
            finally { if (user?.uid == uid && deviceId == id && boardIp == ip) boardBusy = false }
        }
    }

    fun upload(sound: PhoneSound) {
        if (boardBusy || recording) return
        if (!ready || !BoardAudio.validIp(boardIp)) { tell("Kết nối loa và nhập IP trong phần Âm thanh trước khi gửi bản thu."); return }
        val uid = user?.uid; val id = deviceId; val ip = boardIp
        boardBusy = true; transferring = sound.file.name; transferProgress = 0f
        ioJob = viewModelScope.launch {
            try {
                val list = withContext(Dispatchers.IO) {
                    board.upload(ip, id, sound.file) { progress -> main.post {
                        if (user?.uid == uid && deviceId == id && boardIp == ip && transferring == sound.file.name) transferProgress = progress
                    } }
                }
                if (user?.uid == uid && deviceId == id && boardIp == ip) {
                    verifiedUploads = verifiedUploads + sound.file.name
                    prefs.edit().putStringSet("verified_${uid}_$id", verifiedUploads).apply()
                    speakerSounds = list; soundsLoaded = true; tell("Đã gửi và xác minh bản thu trên loa. Bạn có thể chọn làm âm báo.")
                }
            } catch (_: CancellationException) { }
            catch (e: Exception) { if (user?.uid == uid && deviceId == id) tell(e.message ?: "Không gửi được bản thu. Hãy thử lại khi cùng Wi-Fi.") }
            finally { if (user?.uid == uid && deviceId == id && boardIp == ip) { boardBusy = false; transferring = "" } }
        }
    }

    private fun recordingDirectory(): File {
        val uid = user?.uid ?: error("Chưa đăng nhập")
        val key = MessageDigest.getInstance("SHA-256").digest(uid.toByteArray()).take(12).joinToString("") { "%02x".format(it) }
        return File(getApplication<Application>().filesDir, "recordings/$key").apply { mkdirs() }
    }

    private fun readRecordings(): List<PhoneSound> {
        val directory = recordingDirectory()
        return try {
            val metadata = File(directory, "library.json")
            val array = if (metadata.exists()) JSONArray(metadata.readText()) else JSONArray()
            (0 until array.length()).mapNotNull { i ->
                val obj = array.getJSONObject(i); val filename = obj.getString("file")
                if (!filename.matches(Regex("rec_[a-f0-9-]+\\.wav"))) return@mapNotNull null
                val file = File(directory, filename)
                runCatching { PhoneSound(obj.getString("label"), file, WavFormat.durationMillis(file)) }.getOrNull()
            }.reversed()
        } catch (_: Exception) { emptyList() }
    }

    private fun persistRecordings(list: List<PhoneSound>) {
        val array = JSONArray()
        list.asReversed().forEach { array.put(JSONObject().put("file", it.file.name).put("label", it.label)) }
        val atomic = AtomicFile(File(recordingDirectory(), "library.json"))
        val stream = atomic.startWrite()
        try { stream.write(array.toString().toByteArray(Charsets.UTF_8)); atomic.finishWrite(stream) }
        catch (e: Exception) { atomic.failWrite(stream); throw e }
    }

    fun startRecording() {
        if (user == null || recording) return
        if (draft != null) { tell("Lưu hoặc bỏ bản thu hiện tại trước khi thu tiếp."); return }
        if (recordings.size >= 20) { tell("Bạn đã có 20 bản thu. Hãy xóa bớt bản không dùng."); return }
        stopPreview(); recording = true; recordingMs = 0; amplitude = 0f
        val uid = user!!.uid
        val generation = ++recordingGeneration
        val file = File(getApplication<Application>().cacheDir, "draft_${UUID.randomUUID()}.wav")
        try { recorder.start(file, { elapsed, level -> main.post {
            if (alive && user?.uid == uid && generation == recordingGeneration && recording) { recordingMs = elapsed; amplitude = level }
        } }, { result, error -> main.post {
            if (!alive || user?.uid != uid || generation != recordingGeneration) { result?.delete(); return@post }
            recording = false; amplitude = 0f
            if (result != null) draft = PhoneSound("Bản thu mới", result, WavFormat.durationMillis(result))
            else tell(error ?: "Không thể thu âm")
        } }) } catch (_: Exception) { recording = false; file.delete(); tell("Micro đang dừng. Hãy thử lại sau một lát.") }
    }

    fun stopRecording() { if (recording) recorder.stop() }
    fun discardDraft() { stopPreview(); draft?.file?.delete(); draft = null }
    fun saveDraft(label: String) {
        val current = draft ?: return
        if (label.trim().isEmpty() || label.trim().length > 40) { tell("Đặt tên bản thu từ 1 đến 40 ký tự."); return }
        try {
            val target = File(recordingDirectory(), "rec_${UUID.randomUUID()}.wav")
            current.file.copyTo(target)
            val sound = PhoneSound(label.trim(), target, WavFormat.durationMillis(target))
            val list = listOf(sound) + recordings
            try { persistRecordings(list) } catch (e: Exception) { target.delete(); throw e }
            stopPreview(); current.file.delete(); draft = null; recordings = list; tell("Đã lưu bản thu trên điện thoại.")
        } catch (_: Exception) { tell("Không lưu được bản thu. Kiểm tra dung lượng điện thoại.") }
    }

    fun deleteRecording(sound: PhoneSound) {
        if (transferring == sound.file.name) return
        try {
            val next = recordings.filterNot { it.file == sound.file }; persistRecordings(next)
            if (previewing == sound.file.name) stopPreview()
            sound.file.delete(); recordings = next; tell("Đã xóa bản thu trên điện thoại. Tệp đã gửi trên loa vẫn được giữ.")
        } catch (_: Exception) { tell("Chưa xóa được bản thu.") }
    }

    fun preview(sound: PhoneSound) {
        if (recording) return
        if (previewing == sound.file.name) { stopPreview(); return }
        stopPreview()
        try {
            player = MediaPlayer().apply {
                setDataSource(sound.file.absolutePath)
                setOnCompletionListener { stopPreview() }
                setOnErrorListener { _, _, _ -> stopPreview(); tell("Không nghe được bản thu."); true }
                prepare(); start()
            }
            previewing = sound.file.name
        } catch (_: Exception) { stopPreview(); tell("Không nghe được bản thu.") }
    }

    fun stopPreview() { player?.release(); player = null; previewing = "" }
    fun soundLabel(filename: String) = if (filename.isEmpty()) "Âm mặc định" else recordings.find { it.file.name == filename }?.label ?: filename

    override fun onCleared() {
        alive = false; auth.removeAuthStateListener(authListener)
        ownerListener?.let { ownerRef?.removeEventListener(it) }; detachDevice()
        recorder.stop(); stopPreview(); draft?.file?.delete(); super.onCleared()
    }
}
