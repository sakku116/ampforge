plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.ampforge.controller"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.ampforge.controller"
        // BLE GATT server + advertising work on older versions too, but the public
        // Bluetooth MIDI stack and the Android 12+ permission model are clearest from 9.
        minSdk = 28
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"
    }

    signingConfigs {
        // Sideloaded personal app: a committed release keystore keeps the signature
        // stable across updates without Play-managed keys. See android/README.md.
        create("release") {
            storeFile = file("../keystore/controller-release.jks")
            storePassword = "ampforge"
            keyAlias = "ampforge-controller"
            keyPassword = "ampforge"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}
