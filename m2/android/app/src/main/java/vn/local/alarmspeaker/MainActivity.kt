package vn.local.alarmspeaker

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.google.firebase.FirebaseApp
import com.google.firebase.FirebaseOptions
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.database.*
import com.google.firebase.functions.FirebaseFunctions
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import androidx.compose.ui.text.input.PasswordVisualTransformation
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private lateinit var auth: FirebaseAuth
    private lateinit var db: FirebaseDatabase
    private lateinit var functions: FirebaseFunctions
    private var selectedDevice by mutableStateOf("")
    private var desired by mutableStateOf<Map<String, Any>>(emptyMap())
    private var reported by mutableStateOf<Map<String, Any>>(emptyMap())
    private var acks by mutableStateOf<Map<String, Any>>(emptyMap())
    private var status by mutableStateOf("Chưa đăng nhập")
    private var desiredListener: ValueEventListener? = null
    private var reportedListener: ValueEventListener? = null
    private var ackListener: ValueEventListener? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val options = FirebaseOptions.Builder()
            .setProjectId(BuildConfig.FIREBASE_PROJECT_ID)
            .setApplicationId(BuildConfig.FIREBASE_APP_ID)
            .setApiKey(BuildConfig.FIREBASE_API_KEY)
            .setDatabaseUrl(BuildConfig.FIREBASE_DATABASE_URL)
            .build()
        FirebaseApp.initializeApp(this, options)
        auth = FirebaseAuth.getInstance()
        if (BuildConfig.USE_FIREBASE_EMULATOR) auth.useEmulator(BuildConfig.EMULATOR_HOST, 9099)
        db = FirebaseDatabase.getInstance()
        if (BuildConfig.USE_FIREBASE_EMULATOR) db.useEmulator(BuildConfig.EMULATOR_HOST, 9000)
        functions = FirebaseFunctions.getInstance("asia-southeast1")
        if (BuildConfig.USE_FIREBASE_EMULATOR) functions.useEmulator(BuildConfig.EMULATOR_HOST, 5001)
        status = if (BuildConfig.USE_FIREBASE_EMULATOR) "Emulator: ${BuildConfig.EMULATOR_HOST}"
            else "Firebase: ${BuildConfig.FIREBASE_PROJECT_ID}"
        setContent { Screen() }
    }

    private fun ref(path: String) = db.getReference("devices/$selectedDevice/$path")

    private fun watchDevice(id: String) {
        if (!id.matches(Regex("[0-9a-f]{12}"))) { status = "ID thiết bị không hợp lệ"; return }
        if (selectedDevice.isNotEmpty()) {
            desiredListener?.let { ref("inbox/desired").removeEventListener(it) }
            reportedListener?.let { ref("reported").removeEventListener(it) }
            ackListener?.let { ref("acks").removeEventListener(it) }
        }
        selectedDevice = id
        desiredListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                @Suppress("UNCHECKED_CAST")
                desired = snapshot.value as? Map<String, Any> ?: emptyMap()
            }
            override fun onCancelled(error: DatabaseError) { status = error.message }
        }
        reportedListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                @Suppress("UNCHECKED_CAST")
                reported = snapshot.value as? Map<String, Any> ?: emptyMap()
            }
            override fun onCancelled(error: DatabaseError) { status = error.message }
        }
        ackListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                @Suppress("UNCHECKED_CAST")
                acks = snapshot.value as? Map<String, Any> ?: emptyMap()
            }
            override fun onCancelled(error: DatabaseError) { status = error.message }
        }
        ref("inbox/desired").addValueEventListener(desiredListener!!)
        ref("reported").addValueEventListener(reportedListener!!)
        ref("acks").addValueEventListener(ackListener!!)
    }

    private fun updateDesired(change: (MutableMap<String, Any>) -> Unit) {
        ref("inbox/desired").runTransaction(object : Transaction.Handler {
            override fun doTransaction(data: MutableData): Transaction.Result {
                @Suppress("UNCHECKED_CAST")
                val old = data.value as? Map<String, Any> ?: return Transaction.abort()
                val next = old.toMutableMap()
                change(next)
                next["revision"] = ((old["revision"] as? Number)?.toLong() ?: 0L) + 1L
                data.value = next
                return Transaction.success(data)
            }
            override fun onComplete(error: DatabaseError?, committed: Boolean, snapshot: DataSnapshot?) {
                status = if (error != null) error.message else if (committed) "Đã lưu trên Firebase" else "Chưa lưu"
            }
        })
    }

    private fun command(type: String) {
        val now = System.currentTimeMillis()
        ref("inbox/commands").push().setValue(mapOf(
            "type" to type, "issuedAt" to now, "expiresAt" to (now + 30000L)
        )).addOnSuccessListener { status = "Đã gửi lệnh, chờ loa xác nhận" }
            .addOnFailureListener { status = it.message ?: "Gửi lệnh lỗi" }
    }

    private fun pairDevice(id: String, code: String, boardIp: String) {
        if (!id.matches(Regex("[0-9a-f]{12}")) || !code.matches(Regex("[0-9]{6}"))) {
            status = "Device ID hoặc mã ghép không hợp lệ"; return
        }
        if (!BuildConfig.USE_FIREBASE_SPARK) {
            functions.getHttpsCallable("claimDevice").call(mapOf("deviceId" to id, "code" to code))
                .addOnSuccessListener { watchDevice(id); status = "Đã ghép thiết bị" }
                .addOnFailureListener { status = it.message ?: "Ghép lỗi" }
            return
        }
        if (!boardIp.matches(Regex("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}"))) {
            status = "Nhập IP của loa trên Wi-Fi"; return
        }
        val user = auth.currentUser ?: run { status = "Hãy đăng nhập trước khi ghép loa"; return }
        status = "Đang ghép loa qua Wi-Fi…"
        user.getIdToken(true).addOnSuccessListener { token ->
            Thread {
                var connection: HttpURLConnection? = null
                try {
                    val body = JSONObject().put("deviceId", id).put("code", code).put("idToken", token.token).toString()
                    connection = URL("http://$boardIp/api/pair").openConnection() as HttpURLConnection
                    connection.requestMethod = "POST"
                    connection.connectTimeout = 10000
                    connection.readTimeout = 30000
                    connection.doOutput = true
                    connection.setRequestProperty("Content-Type", "application/json")
                    connection.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                    val httpStatus = connection.responseCode
                    val stream = if (httpStatus == 200) connection.inputStream else connection.errorStream
                    val response = stream?.bufferedReader()?.use { it.readText() } ?: ""
                    runOnUiThread {
                        if (httpStatus == 200) { watchDevice(id); status = "Đã ghép loa trên Firebase Spark" }
                        else status = JSONObject(response.ifEmpty { "{}" }).optString("error", "Ghép lỗi HTTP $httpStatus")
                    }
                } catch (error: Exception) {
                    runOnUiThread { status = "Không ghép được loa: ${error.message}" }
                } finally { connection?.disconnect() }
            }.start()
        }.addOnFailureListener { status = it.message ?: "Không lấy được token đăng nhập" }
    }

    @Composable private fun Screen() {
        var email by remember { mutableStateOf("") }
        var password by remember { mutableStateOf("") }
        var id by remember { mutableStateOf("") }
        var code by remember { mutableStateOf("") }
        var boardIp by remember { mutableStateOf("192.168.100.169") }
        var hour by remember { mutableStateOf("07") }
        var minute by remember { mutableStateOf("00") }
        var label by remember { mutableStateOf("Báo thức") }
        var days by remember { mutableStateOf("127") }
        var sound by remember { mutableStateOf("") }
        var duration by remember { mutableStateOf("30") }
        var volume by remember { mutableStateOf("80") }
        var snooze by remember { mutableStateOf("5") }
        var manualSound by remember { mutableStateOf("") }
        var editingId by remember { mutableStateOf<Long?>(null) }
        var now by remember { mutableStateOf(System.currentTimeMillis()) }
        LaunchedEffect(Unit) { while (true) { delay(1000); now = System.currentTimeMillis() } }
        val alarms = (desired["alarms"] as? Map<*, *>) ?: emptyMap<Any, Any>()
        val revision = (desired["revision"] as? Number)?.toLong() ?: 0L
        val applied = (reported["appliedRevision"] as? Number)?.toLong() ?: 0L
        val lastAck = acks.entries.maxByOrNull { (it.value as? Map<*, *>)?.get("at") as? Long ?: 0L }

        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Loa báo thức M2", style = MaterialTheme.typography.headlineSmall)
            Text(status)
            OutlinedTextField(email, { email = it }, label = { Text("Email thử nghiệm") })
            OutlinedTextField(password, { password = it }, label = { Text("Mật khẩu") }, visualTransformation = PasswordVisualTransformation())
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { auth.createUserWithEmailAndPassword(email, password)
                    .addOnSuccessListener { status = "Đã tạo tài khoản" }
                    .addOnFailureListener { status = it.message ?: "Lỗi" } }) { Text("Tạo tài khoản") }
                Button(onClick = { auth.signInWithEmailAndPassword(email, password)
                    .addOnSuccessListener { status = "Đã đăng nhập" }
                    .addOnFailureListener { status = it.message ?: "Lỗi" } }) { Text("Đăng nhập") }
            }
            OutlinedTextField(id, { id = it.trim().lowercase() }, label = { Text("Device ID từ /api/pairing") })
            OutlinedTextField(code, { code = it.trim() }, label = { Text("Mã ghép 6 số") })
            if (BuildConfig.USE_FIREBASE_SPARK) {
                OutlinedTextField(boardIp, { boardIp = it.trim() }, label = { Text("IP loa trên Wi-Fi (chỉ cần khi ghép)") })
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { pairDevice(id, code, boardIp) }) { Text("Ghép loa") }
                Button(onClick = { watchDevice(id) }) { Text("Mở loa đã ghép") }
            }
            if (selectedDevice.isNotEmpty()) {
                Text("Thiết bị: $selectedDevice")
                val lastSeen = (reported["lastSeen"] as? Number)?.toLong() ?: 0L
                val ageSeconds = (now - lastSeen).coerceAtLeast(0L) / 1000L
                Text(if (lastSeen == 0L) "Loa chưa gửi trạng thái lên Firebase"
                    else "Loa: ${if (ageSeconds <= 150) "online" else "offline"} · cập nhật $ageSeconds giây trước")
                Text("Firebase v$revision · Loa v$applied · " +
                    if (applied == revision && revision > 0) "đã đồng bộ" else "đang chờ đồng bộ")
                Text("SD: ${reported["sdMounted"] ?: "?"} · đang phát: ${reported["playing"] ?: "?"}")
                Text("Lệnh gần nhất: ${(lastAck?.value as? Map<*, *>)?.get("state") ?: "chưa có ACK"}")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { command("playNow") }) { Text("Phát ngay") }
                    Button(onClick = { command("stop") }) { Text("Dừng") }
                    Button(onClick = { command("snooze") }) { Text("Báo lại") }
                }
                OutlinedTextField(manualSound, { manualSound = it }, label = { Text("Âm phát ngay (.wav trên SD; trống = mặc định)") })
                Button(onClick = { if (manualSound.isEmpty() || manualSound.matches(Regex("[^/\\\\:]+\\.wav", RegexOption.IGNORE_CASE)))
                    updateDesired { it["manualSound"] = manualSound } else status = "Tên file WAV không hợp lệ" }) {
                    Text("Lưu âm phát ngay")
                }
                HorizontalDivider()
                Text("Báo thức (${alarms.size}/20)", style = MaterialTheme.typography.titleLarge)
                alarms.forEach { (key, value) ->
                    val alarm = value as? Map<*, *> ?: return@forEach
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("${alarm["hour"]}:${alarm["minute"]} ${alarm["name"]}", Modifier.weight(1f))
                        TextButton(onClick = { updateDesired { next ->
                            val all = (next["alarms"] as? Map<*, *>)?.toMutableMap() ?: mutableMapOf()
                            val changed = alarm.toMutableMap()
                            changed["enabled"] = alarm["enabled"] != true
                            all[key] = changed
                            next["alarms"] = all
                        } }) { Text(if (alarm["enabled"] == true) "Tắt" else "Bật") }
                        TextButton(onClick = {
                            editingId = (alarm["id"] as? Number)?.toLong()
                            hour = alarm["hour"].toString()
                            minute = alarm["minute"].toString()
                            days = alarm["days"].toString()
                            duration = alarm["ringSeconds"].toString()
                            volume = alarm["volume"].toString()
                            snooze = alarm["snoozeMinutes"].toString()
                            label = alarm["name"].toString()
                            sound = alarm["sound"].toString()
                        }) { Text("Sửa") }
                        TextButton(onClick = { updateDesired { next ->
                            val all = (next["alarms"] as? Map<*, *>)?.toMutableMap() ?: mutableMapOf()
                            all.remove(key)
                            next["alarms"] = all
                            next["alarmCount"] = all.size
                        } }) { Text("Xóa") }
                    }
                }
                OutlinedTextField(hour, { hour = it }, label = { Text("Giờ 0–23") })
                OutlinedTextField(minute, { minute = it }, label = { Text("Phút 0–59") })
                OutlinedTextField(label, { label = it }, label = { Text("Tên báo thức") })
                Text("Lặp lại (không chọn = một lần)")
                val dayLabels = listOf("CN", "T2", "T3", "T4", "T5", "T6", "T7")
                dayLabels.indices.chunked(4).forEach { row ->
                    Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                        row.forEach { day ->
                            val bit = 1 shl day
                            FilterChip(
                                selected = ((days.toIntOrNull() ?: 0) and bit) != 0,
                                onClick = { days = ((days.toIntOrNull() ?: 0) xor bit).toString() },
                                label = { Text(dayLabels[day]) }
                            )
                        }
                    }
                }
                OutlinedTextField(sound, { sound = it }, label = { Text("Âm .wav trên SD (trống = mặc định)") })
                OutlinedTextField(duration, { duration = it }, label = { Text("Thời lượng reo 5–30 giây") })
                OutlinedTextField(volume, { volume = it }, label = { Text("Âm lượng 0–100") })
                OutlinedTextField(snooze, { snooze = it }, label = { Text("Báo lại 0–60 phút") })
                Button(onClick = {
                    val h = hour.toIntOrNull(); val m = minute.toIntOrNull()
                    val d = days.toIntOrNull(); val seconds = duration.toIntOrNull()
                    val vol = volume.toIntOrNull(); val snoozeMinutes = snooze.toIntOrNull()
                    val editId = editingId
                    val alarmLabel = label
                    val alarmSound = sound
                    if ((editId == null && alarms.size >= 20) || h == null || h !in 0..23 ||
                        m == null || m !in 0..59 || d == null || d !in 0..127 ||
                        seconds == null || seconds !in 5..30 || vol == null || vol !in 0..100 ||
                        snoozeMinutes == null || snoozeMinutes !in 0..60 ||
                        alarmLabel.toByteArray().size >= 32 ||
                        alarmSound.toByteArray().size >= 96 ||
                        (alarmSound.isNotEmpty() && !alarmSound.matches(Regex("[^/\\\\:]+\\.wav", RegexOption.IGNORE_CASE)))) {
                        status = "Kiểm tra giờ, tên, file và giới hạn 20 báo thức/30 giây"
                    } else updateDesired { next ->
                        val alarmId = editId ?: ((next["nextId"] as? Number)?.toLong() ?: 1L)
                        val all = (next["alarms"] as? Map<*, *>)?.toMutableMap() ?: mutableMapOf()
                        val alarmKey = "a_$alarmId"
                        val previous = all[alarmKey] as? Map<*, *>
                        all[alarmKey] = mapOf("id" to alarmId,
                            "enabled" to (previous?.get("enabled") ?: true),
                            "hour" to h!!, "minute" to m!!, "days" to d!!, "volume" to vol!!,
                            "ringSeconds" to seconds!!, "snoozeMinutes" to snoozeMinutes!!,
                            "name" to alarmLabel, "sound" to alarmSound)
                        next["alarms"] = all
                        next["alarmCount"] = all.size
                        if (editId == null) next["nextId"] = alarmId + 1L
                    }
                    editingId = null
                }) { Text(if (editingId == null) "Thêm báo thức" else "Lưu báo thức") }
                Text("Thu âm từ điện thoại và tải WAV sẽ làm ở M3.")
            }
        }
    }
}
