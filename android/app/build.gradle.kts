plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

import java.util.Properties

val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties()
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(keystorePropertiesFile.inputStream())
}

android {
    namespace = "com.crushclaw"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.crushclaw"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "1.1.0"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DPLATFORM_ANDROID=1",
                    "-DCMAKE_BUILD_TYPE=Release",
                )
            }
        }

        ndk {
            // abiFilters handled by splits.abi below
        }
    }

    signingConfigs {
        if (keystorePropertiesFile.exists()) {
            create("release") {
                storeFile = file(keystoreProperties["storeFile"] as String)
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    splits {
        abi {
            isEnable = true
            reset()
            include("arm64-v8a", "armeabi-v7a", "x86_64")
            isUniversalApk = false
        }
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        viewBinding = true
    }
}

tasks.register("renameReleaseApks") {
    dependsOn("assembleRelease")
    doLast {
        val ver = android.defaultConfig.versionName
        val dir = layout.buildDirectory.dir("outputs/apk/release").get().asFile
        dir.listFiles { f -> f.name.endsWith(".apk") }?.forEach { apk ->
            // Handle both signed ("app-<abi>-release.apk") and unsigned names
            val abi = apk.name
                .removePrefix("app-")
                .removeSuffix("-release-unsigned.apk")
                .removeSuffix("-release.apk")
            apk.renameTo(file("${dir}/Crush-Claw-v${ver}-${abi}.apk"))
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.2.0")
    implementation("androidx.lifecycle:lifecycle-service:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
}
