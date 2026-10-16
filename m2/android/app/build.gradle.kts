import groovy.json.JsonSlurper

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "vn.local.alarmspeaker"
    compileSdk = 35
    defaultConfig {
        applicationId = "vn.local.alarmspeaker"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.2-test"
        val host = providers.gradleProperty("emulatorHost").orElse("10.0.2.2").get()
        val useEmulator = providers.gradleProperty("useFirebaseEmulator").orElse("true").get().toBooleanStrict()
        val config = if (!useEmulator) {
            JsonSlurper().parse(file("google-services.json")) as Map<*, *>
        } else null
        val project = config?.get("project_info") as? Map<*, *>
        val client = (config?.get("client") as? List<*>)?.map { it as Map<*, *> }?.single {
            val info = it["client_info"] as Map<*, *>
            (info["android_client_info"] as Map<*, *>)["package_name"] == applicationId
        }
        fun stringField(name: String, value: String) {
            buildConfigField("String", name, groovy.json.JsonOutput.toJson(value))
        }
        buildConfigField("boolean", "USE_FIREBASE_EMULATOR", useEmulator.toString())
        buildConfigField("boolean", "USE_FIREBASE_SPARK", (!useEmulator).toString())
        buildConfigField("String", "EMULATOR_HOST", "\"$host\"")
        stringField("FIREBASE_PROJECT_ID", project?.get("project_id") as? String ?: "demo-alarm")
        stringField("FIREBASE_APP_ID", (client?.get("client_info") as? Map<*, *>)?.get("mobilesdk_app_id") as? String
            ?: "1:1234567890:android:demoalarm")
        stringField("FIREBASE_API_KEY", ((client?.get("api_key") as? List<*>)?.first() as? Map<*, *>)?.get("current_key") as? String
            ?: ("AIza" + "0".repeat(35)))
        stringField("FIREBASE_DATABASE_URL", project?.get("firebase_url") as? String
            ?: "https://demo-alarm-default-rtdb.firebaseio.com")
    }
    buildFeatures { compose = true; buildConfig = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2025.04.01"))
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.ui:ui")
    implementation(platform("com.google.firebase:firebase-bom:33.16.0"))
    implementation("com.google.firebase:firebase-auth")
    implementation("com.google.firebase:firebase-database")
    implementation("com.google.firebase:firebase-functions")
}
