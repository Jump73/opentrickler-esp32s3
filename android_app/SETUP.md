tlo# OpenTrickler Android App — Setup Guide

## Prerequisites

1. Install Flutter SDK: https://docs.flutter.dev/get-started/install/windows
2. Install Android Studio (for SDK + emulator or USB debugging)
3. Enable USB debugging on your Android phone (Developer Options)

---

## 1. Create Flutter project scaffold

From inside the `android_app/` folder:

```bash
cd android_app
flutter create . --org com.opentrickler --project-name opentrickler
```

This generates the `android/`, `ios/`, `test/` directories.
The `lib/` files you already have **will not be overwritten**.

---

## 2. Replace pubspec.yaml

The `pubspec.yaml` in this folder is already configured with the correct
dependencies (`flutter_blue_plus`, `provider`). After running `flutter create`:

```bash
flutter pub get
```

---

## 3. Add BLE permissions to AndroidManifest.xml

Open `android/app/src/main/AndroidManifest.xml` and add the following
**before** the `<application>` tag:

```xml
<!-- BLE permissions for Android 12+ -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.BLUETOOTH_ADVERTISE" />

<!-- Location needed for BLE scan on Android 11 and below -->
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />

<!-- Classic BT (legacy, required by flutter_blue_plus) -->
<uses-permission android:name="android.permission.BLUETOOTH"
    android:maxSdkVersion="30" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN"
    android:maxSdkVersion="30" />

<!-- Declare BLE feature -->
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
```

Also set `minSdkVersion` to 21 in `android/app/build.gradle`:

```gradle
defaultConfig {
    minSdkVersion 21
    targetSdkVersion 34
    ...
}
```

---

## 4. Build and install

```bash
# Debug APK (faster, USB connected phone)
flutter run

# Release APK
flutter build apk --release
# APK will be at: build/app/outputs/flutter-apk/app-release.apk
```

---

## App structure

```
lib/
  main.dart              — App entry point, BleManager provider
  models.dart            — DeviceStatus, Profile data classes
  ble_manager.dart       — BLE connect/disconnect, NUS GATT, commands
  screens/
    scan_screen.dart     — BLE scan, device list, connect
    home_screen.dart     — Bottom navigation shell
    trickler_screen.dart — Charge mode UI
    autotune_screen.dart — Autotune UI
```

---

## BLE Protocol

Communication uses **Nordic UART Service (NUS)** over BLE.

- **ESP → Phone**: JSON status pushed every 300 ms via GATT Notify
  `{"t":"s","w":41.47,"v":1,"cs":2,"ct":4.3,"cg":41.5,...}`
- **Phone → ESP**: JSON commands written to RX characteristic
  `{"cmd":"cstart","w":41.5,"p":0}`

### Commands

| cmd       | params              | action                        |
|-----------|---------------------|-------------------------------|
| cstart    | w (gn), p (idx)     | Start charge                  |
| cstop     |                     | Stop charge                   |
| szero     |                     | Zero scale                    |
| psel      | p (idx)             | Select profile (no charge)    |
| profiles  |                     | Request profile list          |
| atstart   | w, tt, r, cw, fw…   | Start autotune                |
| atcancel  |                     | Cancel autotune               |
| atfinish  |                     | Finish autotune now           |
| cleanup   | s (speed 0.0-1.0)   | Start cleanup mode            |
| cleanstop |                     | Stop cleanup mode             |
