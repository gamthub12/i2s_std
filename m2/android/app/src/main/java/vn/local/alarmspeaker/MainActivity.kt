package vn.local.alarmspeaker

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.ViewModelProvider

class MainActivity : ComponentActivity() {
    private lateinit var model: AlarmModel
    private val permission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) model.startRecording()
        else model.tell("Chưa có quyền micro. Bạn có thể cấp quyền trong Cài đặt ứng dụng để thu âm.")
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        model = ViewModelProvider(this)[AlarmModel::class.java]
        setContent { AlarmSpeakerApp(model) {
            if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) model.startRecording()
            else permission.launch(Manifest.permission.RECORD_AUDIO)
        } }
    }
    override fun onStop() {
        if (::model.isInitialized) { model.stopRecording(); model.stopPreview() }
        super.onStop()
    }
}
