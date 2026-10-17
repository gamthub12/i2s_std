@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
package vn.local.alarmspeaker

import android.app.TimePickerDialog
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.*
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import kotlinx.coroutines.delay
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import kotlin.math.abs
import kotlin.math.sin

private val Blue = Color(0xFF2463EB)
private val Navy = Color(0xFF102753)
private val Muted = Color(0xFF687990)
private val Page = Color(0xFFF3F6FC)
private val Green = Color(0xFF12805C)
private val Palette = lightColorScheme(primary = Blue, onPrimary = Color.White,
    primaryContainer = Color(0xFFE5EEFF), onPrimaryContainer = Navy, secondary = Color(0xFF4278B5),
    background = Page, surface = Color.White, onBackground = Navy, onSurface = Navy,
    onSurfaceVariant = Muted, surfaceVariant = Color(0xFFEDF2FA), outline = Color(0xFFCCD6E5))

@Composable fun AlarmSpeakerApp(model: AlarmModel, record: () -> Unit) {
    MaterialTheme(colorScheme = Palette, shapes = Shapes(small = RoundedCornerShape(12.dp), medium = RoundedCornerShape(20.dp), large = RoundedCornerShape(28.dp))) {
        val snackbar = remember { SnackbarHostState() }
        LaunchedEffect(model.notice?.id) { model.notice?.let { snackbar.showSnackbar(it.text) } }
        Scaffold(containerColor = Page, snackbarHost = { SnackbarHost(snackbar, Modifier.padding(bottom = if (model.user != null) 80.dp else 0.dp)) }, contentWindowInsets = WindowInsets.safeDrawing) { padding ->
            Box(Modifier.fillMaxSize().padding(padding)) {
                key(model.user?.uid) { if (model.user == null) Login(model) else Dashboard(model, record) }
            }
        }
    }
}

@Composable private fun Login(model: AlarmModel) {
    var email by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var create by rememberSaveable { mutableStateOf(false) }
    var visible by remember { mutableStateOf(false) }
    val focus = LocalFocusManager.current
    fun submit() { focus.clearFocus(); model.authenticate(email, password, create) }
    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color(0xFFEAF1FF), Page, Color.White)))) {
        Canvas(Modifier.fillMaxSize()) {
            drawCircle(Blue.copy(alpha = .045f), size.width * .65f, Offset(size.width, 0f))
            drawCircle(Blue.copy(alpha = .035f), size.width * .55f, Offset(0f, size.height * .8f))
        }
        Column(Modifier.fillMaxSize().imePadding().verticalScroll(rememberScrollState()).padding(horizontal = 28.dp, vertical = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(18.dp)) {
            Spacer(Modifier.height(20.dp))
            Box(Modifier.size(76.dp).background(Brush.linearGradient(listOf(Blue, Color(0xFF51A4FF))), RoundedCornerShape(25.dp)), contentAlignment = Alignment.Center) {
                Icon(Icons.Rounded.Alarm, null, tint = Color.White, modifier = Modifier.size(42.dp))
            }
            Text("Loa báo thức", fontSize = 28.sp, fontWeight = FontWeight.Bold)
            Text("Thức dậy cùng âm thanh của bạn.", color = Muted)
            Spacer(Modifier.height(4.dp))
            Panel {
                Text(if (create) "Tạo tài khoản" else "Chào mừng trở lại", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
                Text(if (create) "Lưu lịch và kết nối với loa của bạn." else "Đăng nhập để quản lý loa và báo thức.", color = Muted, fontSize = 13.sp)
                OutlinedTextField(email, { email = it }, Modifier.fillMaxWidth(), label = { Text("Email") }, leadingIcon = { Icon(Icons.Rounded.MailOutline, null) }, singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email, imeAction = ImeAction.Next), enabled = !model.authBusy)
                OutlinedTextField(password, { password = it }, Modifier.fillMaxWidth(), label = { Text("Mật khẩu") }, leadingIcon = { Icon(Icons.Rounded.LockOpen, null) }, singleLine = true,
                    visualTransformation = if (visible) VisualTransformation.None else PasswordVisualTransformation(), trailingIcon = {
                        IconButton(onClick = { visible = !visible }) { Icon(if (visible) Icons.Rounded.VisibilityOff else Icons.Rounded.Visibility, "Hiện hoặc ẩn mật khẩu") }
                    }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { submit() }), enabled = !model.authBusy)
                if (create) Text("Mật khẩu cần ít nhất 6 ký tự.", color = Muted, fontSize = 12.sp)
                Button(onClick = { submit() }, Modifier.fillMaxWidth().height(52.dp), enabled = !model.authBusy) {
                    if (model.authBusy) CircularProgressIndicator(Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp)
                    else Text(if (create) "Tạo tài khoản" else "Đăng nhập", fontWeight = FontWeight.SemiBold)
                }
                TextButton(onClick = { create = !create }, Modifier.align(Alignment.CenterHorizontally), enabled = !model.authBusy) {
                    Text(if (create) "Đã có tài khoản? Đăng nhập" else "Chưa có tài khoản? Đăng ký")
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Icon(Icons.Rounded.VerifiedUser, null, Modifier.size(16.dp), tint = Muted)
                Text("Lịch của bạn. Âm thanh của bạn.", color = Muted, fontSize = 12.sp)
            }
        }
    }
}

@Composable private fun Dashboard(model: AlarmModel, record: () -> Unit) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var device by remember { mutableStateOf(false) }
    var account by remember { mutableStateOf(false) }
    var editing by remember { mutableStateOf<AlarmItem?>(null) }
    var deleting by remember { mutableStateOf<AlarmItem?>(null) }
    var manual by remember { mutableStateOf(false) }
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) { while (true) { now = System.currentTimeMillis(); delay(1000) } }
    Scaffold(containerColor = Page, contentWindowInsets = WindowInsets(0), topBar = {
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).background(Blue, RoundedCornerShape(13.dp)), contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Alarm, null, tint = Color.White) }
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Text("Loa báo thức", fontWeight = FontWeight.Bold, fontSize = 20.sp)
                Text(listOf("Một ngày mới, đúng nhịp", "Lịch của bạn", "Âm thanh mang dấu ấn riêng")[tab], color = Muted, fontSize = 12.sp)
            }
            IconButton(onClick = { account = true }) { Icon(Icons.Rounded.AccountCircle, "Tài khoản và đăng xuất", tint = Blue, modifier = Modifier.size(30.dp)) }
        }
    }, bottomBar = {
        NavigationBar(containerColor = Color.White, tonalElevation = 0.dp, windowInsets = WindowInsets(0)) {
            listOf("Tổng quan" to Icons.Rounded.GridView, "Báo thức" to Icons.Rounded.Alarm, "Âm thanh" to Icons.Rounded.GraphicEq).forEachIndexed { index, item ->
                NavigationBarItem(selected = tab == index, onClick = { tab = index }, icon = { Icon(item.second, null) }, label = { Text(item.first) },
                    colors = NavigationBarItemDefaults.colors(indicatorColor = Color(0xFFE7EFFF), selectedIconColor = Blue, selectedTextColor = Blue))
            }
        }
    }) { padding ->
        when (tab) {
            0 -> Home(model, model.online(now), Modifier.padding(padding), { device = true }, { tab = 1 }, { tab = 2 }, { manual = true })
            1 -> Alarms(model, Modifier.padding(padding), { editing = AlarmItem() }, { editing = it }, { deleting = it })
            else -> Sounds(model, record, Modifier.padding(padding)) { editing = AlarmItem(sound = it) }
        }
    }
    if (device) DeviceDialog(model) { device = false }
    if (account) AlertDialog(onDismissRequest = { account = false }, icon = { Icon(Icons.Rounded.AccountCircle, null, tint = Blue) }, title = { Text("Tài khoản của bạn") },
        text = { Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(model.user?.email.orEmpty()); Text("Đăng xuất không xóa các báo thức đã lưu trên loa. Bản thu được lưu riêng theo tài khoản trên điện thoại.", color = Muted)
            if (model.recording || model.draft != null) Text("Bản thu chưa lưu sẽ bị bỏ khi đăng xuất.", color = MaterialTheme.colorScheme.error)
        } }, confirmButton = { TextButton(onClick = { account = false; model.logout() }) { Icon(Icons.AutoMirrored.Rounded.Logout, null, Modifier.size(18.dp)); Spacer(Modifier.width(8.dp)); Text("Đăng xuất") } },
        dismissButton = { TextButton(onClick = { account = false }) { Text("Đóng") } })
    editing?.let { item -> AlarmEditor(model, item, { editing = null }) { saved -> model.saveAlarm(saved) { editing = null } } }
    deleting?.let { item -> AlertDialog(onDismissRequest = { deleting = null }, title = { Text("Xóa báo thức?") }, text = { Text("${item.name} lúc ${timeText(item.hour, item.minute)} sẽ được xóa khỏi lịch.") },
        confirmButton = { TextButton(onClick = { model.deleteAlarm(item); deleting = null }) { Text("Xóa", color = MaterialTheme.colorScheme.error) } },
        dismissButton = { TextButton(onClick = { deleting = null }) { Text("Giữ lại") } }) }
    if (manual) SoundPicker(model, model.manualSound, { manual = false }) { model.setManualSound(it); manual = false }
}

@Composable private fun Home(model: AlarmModel, online: Boolean, modifier: Modifier, device: () -> Unit, alarms: () -> Unit, sounds: () -> Unit, manual: () -> Unit) {
    val next = nextAlarm(model.alarms)
    LazyColumn(modifier.fillMaxSize(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(18.dp)) {
        item { Box(Modifier.fillMaxWidth().background(Brush.linearGradient(listOf(Color(0xFF173E9C), Blue, Color(0xFF4286F4))), RoundedCornerShape(28.dp))) {
            Canvas(Modifier.matchParentSize()) { repeat(3) { i -> drawCircle(Color.White.copy(alpha = .085f), size.width * (.22f + i * .1f), Offset(size.width, size.height * .25f), style = Stroke(2.dp.toPx())) } }
            Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                    Text("BÁO THỨC TIẾP THEO", color = Color.White.copy(alpha = .8f), fontSize = 11.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
                    Surface(color = Color.White.copy(alpha = .15f), shape = CircleShape) { Text(if (online) "● Online" else "● Offline", Modifier.padding(horizontal = 10.dp, vertical = 5.dp), color = Color.White, fontSize = 11.sp) }
                }
                Text(if (model.deviceId.isEmpty()) "Chào ngày mới" else next?.second?.format(DateTimeFormatter.ofPattern("HH:mm")) ?: "Thảnh thơi thôi",
                    fontSize = if (next != null && model.deviceId.isNotEmpty()) 52.sp else 30.sp, color = Color.White, fontWeight = FontWeight.Bold)
                Text(if (model.deviceId.isEmpty()) "Kết nối loa để bắt đầu một buổi sáng theo cách của bạn." else if (next != null) "${next.first.name} · ${next.second.format(DateTimeFormatter.ofPattern("dd/MM"))}" else "Chưa có báo thức đang bật. Thêm một lịch mới nhé.", color = Color.White.copy(alpha = .9f), fontSize = 14.sp)
                if (model.deviceId.isNotEmpty() && model.reported["clockSynced"] != true) Text("Loa đang chờ đồng bộ giờ", color = Color.White, fontSize = 12.sp)
            }
        } }
        item { Panel {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Tile(Icons.Rounded.Speaker)
                Column(Modifier.weight(1f).padding(horizontal = 12.dp)) { Text(if (model.deviceId.isEmpty()) "Kết nối loa của bạn" else "Loa báo thức", fontWeight = FontWeight.Bold); Text(model.deviceId.ifEmpty { "Ghép một lần, dùng mỗi ngày" }, color = Muted, fontSize = 12.sp) }
                IconButton(onClick = device) { Icon(Icons.Rounded.Tune, "Kết nối hoặc đổi loa", tint = Blue) }
            }
            if (model.deviceId.isEmpty()) Button(onClick = device, Modifier.fillMaxWidth()) { Text("Kết nối thiết bị") }
            else {
                HorizontalDivider(color = Page)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Detail("Thẻ nhớ", if (model.reported["sdMounted"] == true) "Sẵn sàng" else "Chưa sẵn sàng", model.reported["sdMounted"] == true)
                    Detail("Trạng thái", if (model.playing) "Đang phát" else "Đang nghỉ", online)
                }
                Surface(color = Page, shape = RoundedCornerShape(12.dp)) { Row(Modifier.fillMaxWidth().padding(12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(if (model.ready && model.revision == model.applied) Icons.Rounded.CloudDone else Icons.Rounded.CloudSync, null, Modifier.size(18.dp), tint = Blue)
                    Text(when { model.accessError.isNotEmpty() -> model.accessError; !model.ready -> "Đang tải lịch của bạn…"; model.revision == model.applied -> "Lịch đã đồng bộ với loa · v${model.revision}"; else -> "Đã lưu v${model.revision} · đang chờ loa nhận" }, fontSize = 12.sp)
                } }
            }
        } }
        if (model.deviceId.isNotEmpty()) item { Panel {
            Heading("Điều khiển nhanh", "Một chạm để nghe âm thanh yêu thích")
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(onClick = { model.command("playNow") }, Modifier.weight(1f).height(50.dp), enabled = online && model.ready) { Icon(Icons.Rounded.PlayArrow, null); Spacer(Modifier.width(5.dp)); Text("Phát ngay") }
                OutlinedButton(onClick = { model.command("stop") }, Modifier.weight(1f).height(50.dp), enabled = online && model.ready) { Icon(Icons.Rounded.Stop, null); Spacer(Modifier.width(5.dp)); Text("Dừng") }
            }
            Row(Modifier.fillMaxWidth().clickable(enabled = model.ready, onClick = manual).padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.MusicNote, null, tint = Blue, modifier = Modifier.size(20.dp))
                Column(Modifier.weight(1f).padding(horizontal = 10.dp)) { Text("Âm phát ngay", fontSize = 12.sp, color = Muted); Text(model.soundLabel(model.manualSound), maxLines = 1, overflow = TextOverflow.Ellipsis) }
                Icon(Icons.Rounded.ChevronRight, null, tint = Muted)
            }
            if (model.playing) TextButton(onClick = { model.command("snooze") }, enabled = online) { Icon(Icons.Rounded.Snooze, null); Spacer(Modifier.width(6.dp)); Text("Báo lại") }
            model.lastAck?.let { Text(if (it == "done") "✓ Loa đã nhận lệnh gần nhất" else if (it == "expired") "Lệnh gần nhất đã hết hạn" else "Loa chưa thực hiện được lệnh gần nhất", fontSize = 11.sp, color = if (it == "done") Green else Muted) }
        } }
        item { Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Shortcut("${model.alarms.count { it.enabled }} đang bật", "Báo thức", Icons.Rounded.Alarm, Modifier.weight(1f), alarms)
            Shortcut("Giọng nói của bạn", "Thu âm làm chuông", Icons.Rounded.Mic, Modifier.weight(1f), sounds)
        } }
    }
}

@Composable private fun Alarms(model: AlarmModel, modifier: Modifier, add: () -> Unit, edit: (AlarmItem) -> Unit, delete: (AlarmItem) -> Unit) {
    LazyColumn(modifier.fillMaxSize(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        item { Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) { Text("Báo thức của bạn", fontSize = 24.sp, fontWeight = FontWeight.Bold); Text("${model.alarms.size}/20 lịch · Giờ Việt Nam", color = Muted, fontSize = 12.sp) }
            FilledIconButton(onClick = add, enabled = model.ready && model.alarms.size < 20 && !model.saveBusy) { Icon(Icons.Rounded.Add, "Thêm báo thức") }
        } }
        if (!model.ready) item { Empty(Icons.Rounded.Speaker, "Kết nối loa trước nhé", "Chọn loa tại Tổng quan để tải và quản lý lịch báo thức.") }
        else if (model.alarms.isEmpty()) item { Empty(Icons.Rounded.WbSunny, "Buổi sáng bắt đầu từ đây", "Chạm dấu + để tạo báo thức đầu tiên.") }
        items(model.alarms, key = { it.id }) { alarm -> Panel {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f).clickable { edit(alarm) }) { Text(timeText(alarm.hour, alarm.minute), fontSize = 36.sp, fontWeight = FontWeight.Bold, color = if (alarm.enabled) Navy else Muted); Text(alarm.name, fontWeight = FontWeight.Medium) }
                Switch(alarm.enabled, { model.toggleAlarm(alarm) }, enabled = !model.saveBusy)
            }
            Text(dayText(alarm.days), color = if (alarm.enabled) Blue else Muted, fontSize = 12.sp)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.MusicNote, null, Modifier.size(16.dp), tint = Muted)
                Text(model.soundLabel(alarm.sound), Modifier.weight(1f).padding(start = 5.dp), color = Muted, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                IconButton(onClick = { edit(alarm) }, Modifier.size(36.dp)) { Icon(Icons.Rounded.Edit, "Sửa ${alarm.name}", Modifier.size(19.dp), tint = Blue) }
                IconButton(onClick = { delete(alarm) }, Modifier.size(36.dp), enabled = !model.saveBusy) { Icon(Icons.Rounded.DeleteOutline, "Xóa ${alarm.name}", Modifier.size(19.dp), tint = Muted) }
            }
        } }
        if (model.saveBusy) item { LinearProgressIndicator(Modifier.fillMaxWidth()) }
    }
}

@Composable private fun Sounds(model: AlarmModel, record: () -> Unit, modifier: Modifier, createAlarm: (String) -> Unit) {
    var ip by rememberSaveable(model.deviceId, model.boardIp) { mutableStateOf(model.boardIp) }
    var name by rememberSaveable(model.draft?.file?.name) { mutableStateOf("Bản thu của tôi") }
    var deleting by remember { mutableStateOf<PhoneSound?>(null) }
    val context = LocalContext.current
    LaunchedEffect(Unit) { if (model.boardIp.isNotEmpty() && !model.soundsLoaded) model.refreshSounds() }
    LazyColumn(modifier.fillMaxSize().imePadding(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item { Heading("Thư viện âm thanh", "Một lời nhắn, một giai điệu, một buổi sáng riêng.", true) }
        item { Panel {
            Row(verticalAlignment = Alignment.CenterVertically) { Tile(Icons.Rounded.Mic); Column(Modifier.weight(1f).padding(start = 12.dp)) { Text("Thu âm làm chuông", fontWeight = FontWeight.Bold); Text("Tối đa 30 giây · WAV rõ nét", fontSize = 12.sp, color = Muted) } }
            if (model.recording) {
                Waveform(model.amplitude)
                Text("${durationText(model.recordingMs)} / 00:30", Modifier.align(Alignment.CenterHorizontally), fontSize = 30.sp, fontWeight = FontWeight.Bold, color = Blue)
                Text("Đang thu… tự dừng ở giây 30.", color = Muted, fontSize = 12.sp)
                Button(onClick = model::stopRecording, Modifier.fillMaxWidth(), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFD94B60))) { Icon(Icons.Rounded.Stop, null); Spacer(Modifier.width(8.dp)); Text("Dừng thu âm") }
            } else if (model.draft != null) {
                val draft = model.draft!!
                Text("Bản thu sẵn sàng · ${durationText(draft.durationMs)}", color = Green, fontSize = 13.sp)
                Field(name, { name = it }, "Tên bản thu")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = { model.preview(draft) }, Modifier.weight(1f)) { Icon(if (model.previewing == draft.file.name) Icons.Rounded.Stop else Icons.Rounded.PlayArrow, null); Text("Nghe thử") }
                    Button(onClick = { model.saveDraft(name) }, Modifier.weight(1f)) { Text("Lưu bản thu") }
                }
                TextButton(onClick = model::discardDraft) { Text("Bỏ bản thu này", color = Muted) }
            } else {
                Button(onClick = record, Modifier.fillMaxWidth().height(48.dp), enabled = !model.boardBusy) { Icon(Icons.Rounded.Mic, null); Spacer(Modifier.width(8.dp)); Text("Bắt đầu thu âm") }
                TextButton(onClick = { context.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}"))) }) { Text("Quản lý quyền micro", fontSize = 12.sp) }
            }
        } }
        item { Heading("Trên điện thoại", "${model.recordings.size}/20 bản thu · nghe thử trước khi gửi") }
        if (model.recordings.isEmpty()) item { Text("Chưa có bản thu. Hãy thu một lời chào cho buổi sáng.", color = Muted, fontSize = 13.sp) }
        items(model.recordings, key = { "phone_${it.file.name}" }) { sound ->
            val onBoard = model.soundsLoaded && model.selectableSounds.any { it.filename == sound.file.name }
            Panel {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    FilledTonalIconButton(onClick = { model.preview(sound) }, enabled = !model.recording) { Icon(if (model.previewing == sound.file.name) Icons.Rounded.Stop else Icons.Rounded.PlayArrow, "Nghe thử ${sound.label}") }
                    Column(Modifier.weight(1f).padding(horizontal = 12.dp)) { Text(sound.label, fontWeight = FontWeight.SemiBold, maxLines = 2); Text("${durationText(sound.durationMs)} · ${sound.file.length() / 1024} KB${if (onBoard) " · Đã có trên loa" else ""}", color = if (onBoard) Green else Muted, fontSize = 11.sp) }
                    IconButton(onClick = { deleting = sound }, enabled = model.transferring != sound.file.name) { Icon(Icons.Rounded.DeleteOutline, "Xóa bản thu trên điện thoại", tint = Muted) }
                }
                if (model.transferring == sound.file.name) {
                    LinearProgressIndicator(progress = { model.transferProgress }, modifier = Modifier.fillMaxWidth())
                    Text(if (model.transferProgress < .8f) "Đang gửi lên loa…" else "Đang xác minh tệp trên thẻ SD…", color = Blue, fontSize = 12.sp)
                } else if (!onBoard) OutlinedButton(onClick = { model.upload(sound) }, Modifier.fillMaxWidth(), enabled = model.ready && !model.boardBusy && !model.recording) { Icon(Icons.AutoMirrored.Rounded.Send, null, Modifier.size(18.dp)); Spacer(Modifier.width(8.dp)); Text("Gửi lên loa") }
                else Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { model.setManualSound(sound.file.name) }, Modifier.weight(1f), enabled = !model.saveBusy) { Text("Dùng phát ngay", fontSize = 12.sp) }
                    TextButton(onClick = { createAlarm(sound.file.name) }, Modifier.weight(1f), enabled = !model.saveBusy) { Text("Tạo báo thức", fontSize = 12.sp) }
                }
            }
        }
        item { Panel {
            Heading("Kết nối thẻ nhớ", "Điện thoại và loa cần cùng Wi-Fi để gửi bản thu.")
            Field(ip, { ip = it.trim() }, "IP của loa", KeyboardType.Decimal)
            OutlinedButton(onClick = { model.connectAudio(ip) }, Modifier.fillMaxWidth(), enabled = model.deviceId.isNotEmpty() && !model.boardBusy) { Icon(Icons.Rounded.Refresh, null, Modifier.size(18.dp)); Spacer(Modifier.width(6.dp)); Text("Kết nối và tải danh sách") }
            if (model.boardBusy && model.transferring.isEmpty()) LinearProgressIndicator(Modifier.fillMaxWidth())
        } }
        item { Heading("Trên loa", if (model.soundsLoaded) "${model.speakerSounds.size} tệp WAV trên thẻ SD" else "Nhập IP ở trên để xem các âm đã có") }
        item { SoundRow("Âm mặc định", "Luôn sẵn sàng, kể cả khi không có SD", model.manualSound.isEmpty()) { model.setManualSound("") } }
        items(model.selectableSounds, key = { "speaker_${it.filename}" }) { sound -> SoundRow(model.soundLabel(sound.filename), "${sound.bytes / 1024} KB · Chạm để dùng phát ngay", model.manualSound == sound.filename) { model.setManualSound(sound.filename) } }
    }
    deleting?.let { sound -> AlertDialog(onDismissRequest = { deleting = null }, title = { Text("Xóa bản thu trên điện thoại?") }, text = { Text("“${sound.label}” sẽ được xóa khỏi điện thoại. Bản đã gửi trên loa và lịch báo thức không bị xóa.") },
        confirmButton = { TextButton(onClick = { model.deleteRecording(sound); deleting = null }) { Text("Xóa", color = MaterialTheme.colorScheme.error) } }, dismissButton = { TextButton(onClick = { deleting = null }) { Text("Giữ lại") } }) }
}

@Composable private fun DeviceDialog(model: AlarmModel, dismiss: () -> Unit) {
    var id by remember { mutableStateOf(model.deviceId) }; var ip by remember { mutableStateOf(model.boardIp) }
    var code by remember { mutableStateOf("") }; var pairing by remember { mutableStateOf(false) }
    AlertDialog(onDismissRequest = { if (!model.pairBusy) dismiss() }, icon = { Icon(Icons.Rounded.Speaker, null, tint = Blue) }, title = { Text("Kết nối loa") }, text = {
        Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Mở loa đã ghép hoặc ghép một thiết bị mới.", color = Muted)
            model.ownedDevices.forEach { owned -> AssistChip(onClick = { id = owned }, label = { Text(owned) }, leadingIcon = { Icon(Icons.Rounded.Speaker, null, Modifier.size(16.dp)) }) }
            Field(id, { id = it.trim().lowercase() }, "Device ID"); Field(ip, { ip = it.trim() }, "IP loa trên Wi-Fi", KeyboardType.Decimal)
            Text("IP cần khi ghép lần đầu và gửi bản thu.", color = Muted, fontSize = 12.sp)
            Row(verticalAlignment = Alignment.CenterVertically) { Switch(pairing, { pairing = it }); Text("Ghép loa mới", Modifier.padding(start = 8.dp)) }
            if (pairing) Field(code, { code = it.filter(Char::isDigit).take(6) }, "Mã ghép 6 số", KeyboardType.Number)
            if (model.pairBusy) LinearProgressIndicator(Modifier.fillMaxWidth())
        }
    }, confirmButton = { Button(onClick = {
        if (pairing) model.pair(id, code, ip)
        else { model.watchDevice(id, ip); if (id.matches(Regex("[0-9a-f]{12}")) && (ip.isBlank() || BoardAudio.validIp(ip))) dismiss() }
    }, enabled = !model.pairBusy) { Text(if (pairing) "Ghép loa" else "Mở loa") } }, dismissButton = { TextButton(onClick = dismiss, enabled = !model.pairBusy) { Text("Đóng") } })
}

@Composable private fun AlarmEditor(model: AlarmModel, initial: AlarmItem, dismiss: () -> Unit, save: (AlarmItem) -> Unit) {
    var name by rememberSaveable { mutableStateOf(initial.name) }; var hour by rememberSaveable { mutableIntStateOf(initial.hour) }
    var minute by rememberSaveable { mutableIntStateOf(initial.minute) }; var days by rememberSaveable { mutableIntStateOf(initial.days) }
    var volume by rememberSaveable { mutableFloatStateOf(initial.volume.toFloat()) }; var duration by rememberSaveable { mutableFloatStateOf(initial.ringSeconds.toFloat()) }
    var snooze by rememberSaveable { mutableFloatStateOf(initial.snoozeMinutes.toFloat()) }; var sound by rememberSaveable { mutableStateOf(initial.sound) }
    var choosing by remember { mutableStateOf(false) }; val context = LocalContext.current
    val focus = LocalFocusManager.current
    val keyboard = LocalSoftwareKeyboardController.current
    Dialog(onDismissRequest = { if (!model.saveBusy) dismiss() }, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Surface(Modifier.fillMaxSize(), color = Page) {
            Scaffold(containerColor = Page, contentWindowInsets = WindowInsets.safeDrawing, topBar = {
                TopAppBar(title = { Text(if (initial.id == 0L) "Báo thức mới" else "Sửa báo thức", fontWeight = FontWeight.Bold) }, navigationIcon = {
                    IconButton(onClick = dismiss, enabled = !model.saveBusy) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, "Quay lại") }
                }, colors = TopAppBarDefaults.topAppBarColors(containerColor = Page))
            }, bottomBar = {
                Box(Modifier.fillMaxWidth().imePadding().navigationBarsPadding().padding(20.dp)) {
                Button(onClick = { focus.clearFocus(); keyboard?.hide(); save(initial.copy(name = name.trim(), hour = hour, minute = minute, days = days, volume = volume.toInt(), ringSeconds = duration.toInt(), snoozeMinutes = snooze.toInt(), sound = sound)) },
                    Modifier.fillMaxWidth().height(52.dp), enabled = !model.saveBusy && name.isNotBlank()) {
                    if (model.saveBusy) CircularProgressIndicator(Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp) else Text("Lưu báo thức")
                }
                }
            }) { padding -> Column(Modifier.fillMaxSize().padding(padding).imePadding().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(18.dp)) {
                Card(onClick = { TimePickerDialog(context, { _, h, m -> hour = h; minute = m }, hour, minute, true).show() }, modifier = Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color(0xFFE5EEFF))) {
                    Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) { Text(timeText(hour, minute), fontSize = 56.sp, fontWeight = FontWeight.Bold, color = Blue); Text("Chạm để chọn giờ · Giờ Việt Nam", color = Muted, fontSize = 12.sp) }
                }
                OutlinedTextField(name, { name = it }, Modifier.fillMaxWidth(), label = { Text("Tên báo thức") }, singleLine = true, isError = name.toByteArray().size >= 32,
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done), keyboardActions = KeyboardActions(onDone = { focus.clearFocus(); keyboard?.hide() }),
                    supportingText = { if (name.toByteArray().size >= 32) Text("Tên quá dài. Hãy dùng tên ngắn hơn.") })
                Panel { Heading("Lặp lại", dayText(days)); listOf(0..3, 4..6).forEach { row -> Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    row.forEach { day -> FilterChip(selected = days and (1 shl day) != 0, onClick = { days = days xor (1 shl day) }, label = { Text(listOf("CN", "T2", "T3", "T4", "T5", "T6", "T7")[day]) }) }
                } } }
                Panel {
                    Row(Modifier.fillMaxWidth().clickable { choosing = true }, verticalAlignment = Alignment.CenterVertically) { Tile(Icons.Rounded.MusicNote); Column(Modifier.weight(1f).padding(horizontal = 12.dp)) { Text("Âm báo thức", color = Muted, fontSize = 12.sp); Text(model.soundLabel(sound), maxLines = 2) }; Icon(Icons.Rounded.ChevronRight, null, tint = Muted) }
                    LabeledSlider("Âm lượng", "${volume.toInt()}%", volume, { volume = it }, 0f..100f, 99)
                    LabeledSlider("Thời gian reo", "${duration.toInt()} giây", duration, { duration = it }, 5f..30f, 24)
                    LabeledSlider("Báo lại sau", if (snooze.toInt() == 0) "Tắt" else "${snooze.toInt()} phút", snooze, { snooze = it }, 0f..60f, 59)
                }
            } }
        }
        if (choosing) SoundPicker(model, sound, { choosing = false }) { sound = it; choosing = false }
    }
}

@Composable private fun SoundPicker(model: AlarmModel, selected: String, dismiss: () -> Unit, select: (String) -> Unit) {
    LaunchedEffect(Unit) { if (model.boardIp.isNotEmpty() && !model.soundsLoaded) model.refreshSounds() }
    ModalBottomSheet(onDismissRequest = dismiss, containerColor = Color.White) {
        LazyColumn(Modifier.fillMaxWidth().heightIn(max = 520.dp), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            item { Heading("Chọn âm báo", "Bản thu cần được gửi lên loa trước khi chọn.", true) }
            item { SoundRow("Âm mặc định", "Luôn sẵn sàng trên loa", selected.isEmpty()) { select("") } }
            items(model.selectableSounds, key = { it.filename }) { sound -> SoundRow(model.soundLabel(sound.filename), "${sound.bytes / 1024} KB · WAV trên thẻ SD", selected == sound.filename) { select(sound.filename) } }
            if (model.boardBusy) item { LinearProgressIndicator(Modifier.fillMaxWidth()) }
            if (!model.soundsLoaded && !model.boardBusy) item { Text("Vào Âm thanh, nhập IP loa và tải danh sách để chọn WAV hoặc bản thu của bạn.", fontSize = 13.sp, color = Muted) }
            item { TextButton(onClick = model::refreshSounds, enabled = !model.boardBusy) { Icon(Icons.Rounded.Refresh, null); Spacer(Modifier.width(6.dp)); Text("Làm mới danh sách") } }
        }
    }
}

@Composable private fun Panel(content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.White)) { Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp), content = content) }
}
@Composable private fun Heading(title: String, subtitle: String, large: Boolean = false) {
    Column(verticalArrangement = Arrangement.spacedBy(5.dp)) { Text(title, fontSize = if (large) 24.sp else 17.sp, fontWeight = FontWeight.Bold); Text(subtitle, color = Muted, fontSize = 12.sp) }
}
@Composable private fun Field(value: String, change: (String) -> Unit, label: String, keyboard: KeyboardType = KeyboardType.Text) {
    val focus = LocalFocusManager.current
    val controller = LocalSoftwareKeyboardController.current
    OutlinedTextField(value, change, Modifier.fillMaxWidth(), label = { Text(label) }, singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = keyboard, imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = { focus.clearFocus(); controller?.hide() }))
}
@Composable private fun Tile(icon: ImageVector) {
    Box(Modifier.size(44.dp).background(Color(0xFFEAF1FF), RoundedCornerShape(14.dp)), contentAlignment = Alignment.Center) { Icon(icon, null, tint = Blue) }
}
@Composable private fun Detail(label: String, value: String, good: Boolean) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) { Text(label, color = Muted, fontSize = 12.sp); Text(value, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = if (good) Green else Muted) }
}
@Composable private fun Shortcut(subtitle: String, title: String, icon: ImageVector, modifier: Modifier, click: () -> Unit) {
    Card(onClick = click, modifier = modifier, colors = CardDefaults.cardColors(containerColor = Color.White)) { Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) { Icon(icon, null, tint = Blue, modifier = Modifier.size(26.dp)); Text(title, fontWeight = FontWeight.Bold, fontSize = 14.sp); Text(subtitle, color = Muted, fontSize = 11.sp) } }
}
@Composable private fun Empty(icon: ImageVector, title: String, subtitle: String) { Panel { Icon(icon, null, Modifier.size(42.dp), tint = Blue); Text(title, fontWeight = FontWeight.Bold); Text(subtitle, color = Muted, fontSize = 13.sp) } }
@Composable private fun SoundRow(title: String, subtitle: String, selected: Boolean, click: () -> Unit) {
    Card(onClick = click, colors = CardDefaults.cardColors(containerColor = if (selected) Color(0xFFE8F0FF) else Color.White)) { Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Rounded.MusicNote, null, tint = Blue); Column(Modifier.weight(1f).padding(horizontal = 12.dp)) { Text(title, fontWeight = FontWeight.Medium, maxLines = 2); Text(subtitle, fontSize = 11.sp, color = Muted) }; if (selected) Icon(Icons.Rounded.CheckCircle, "Đang chọn", tint = Blue)
    } }
}
@Composable private fun LabeledSlider(label: String, value: String, current: Float, change: (Float) -> Unit, range: ClosedFloatingPointRange<Float>, steps: Int) {
    Column { Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) { Text(label, fontSize = 13.sp); Text(value, color = Blue, fontSize = 13.sp, fontWeight = FontWeight.SemiBold) }; Slider(current, change, valueRange = range, steps = steps) }
}
@Composable private fun Waveform(level: Float) {
    var phase by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(Unit) { while (true) { phase += .35f; delay(60) } }
    Canvas(Modifier.fillMaxWidth().height(66.dp)) { val spacing = size.width / 35
        repeat(35) { i -> val wave = (.25f + .75f * abs(sin(i * .73f + phase))) * (.15f + level.coerceIn(0f, 1f) * .85f); val height = (size.height * wave).coerceAtLeast(6.dp.toPx())
            drawLine(Blue.copy(alpha = .5f + wave * .5f), Offset(spacing * (i + .5f), (size.height - height) / 2), Offset(spacing * (i + .5f), (size.height + height) / 2), strokeWidth = 4.dp.toPx(), cap = StrokeCap.Round)
        }
    }
}
private fun timeText(hour: Int, minute: Int) = "%02d:%02d".format(hour, minute)
private fun durationText(ms: Long) = "%02d:%02d".format(ms / 60000, ms / 1000 % 60)
private fun dayText(days: Int) = when (days) { 0 -> "Một lần"; 127 -> "Mỗi ngày"; 62 -> "Thứ Hai – Thứ Sáu"; else -> (0..6).filter { days and (1 shl it) != 0 }.joinToString(" · ") { listOf("CN", "T2", "T3", "T4", "T5", "T6", "T7")[it] } }
private fun nextAlarm(alarms: List<AlarmItem>): Pair<AlarmItem, LocalDateTime>? {
    val now = LocalDateTime.now(ZoneId.of("Asia/Ho_Chi_Minh"))
    return alarms.filter { it.enabled }.mapNotNull { alarm -> (0..7).map { now.toLocalDate().plusDays(it.toLong()).atTime(alarm.hour, alarm.minute) }
        .firstOrNull { it.isAfter(now) && (alarm.days == 0 || alarm.days and (1 shl (it.dayOfWeek.value % 7)) != 0) }?.let { alarm to it } }.minByOrNull { it.second }
}
