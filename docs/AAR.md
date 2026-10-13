# AAR builds and QNN integration

**English** · [Русский](AAR.ru.md)

YoloKit releases will include an AAR without model weights. Published builds
are listed in [Releases](https://github.com/gd182/YoloKit/releases).
You can also build the AAR from source as described below. For the API and
lifecycle methods, see [the library README](../README.md).

## Build

Set `qnn.sdk.dir` in this project's `local.properties` to the QAIRT 2.50.0
SDK directory, or pass `-Pqnn.sdk.dir=/path/to/QAIRT/2.50.0` to Gradle.
Without the SDK headers the native library only supports ncnn.

```bash
./gradlew :yolokit:assembleRelease
```

Output: `yolokit/build/outputs/aar/yolokit-release.aar`.
Release builds use R8 to obfuscate internal JVM implementation classes.
Public API names and the two JNI bridge classes remain stable. R8 does not
obfuscate C++ code, model files, or Qualcomm's native libraries.
Keep `yolokit/build/outputs/mapping/release/mapping.txt` with each distributed
release for decoding stack traces; it is not needed by consuming apps.

## Release hardening

The native library registers its methods through `RegisterNatives` and exports
only `JNI_OnLoad` and `JNI_OnUnload`. Internal C++ symbols are hidden by a
linker version script. Release packaging removes debug symbols, and detailed
tensor and timing diagnostics are compiled only into debug builds. Error logs
remain available.

R8 keeps the public API and the JNI class/method names required for registration,
while allowing other internal members to be removed or obfuscated. This raises
the effort of analyzing an AAR; it does not encrypt native code or model files.

Check a built release AAR with the supplied script (macOS or Linux, JDK and NDK
tools required):

```bash
bash scripts/check_release_aar.sh \
  yolokit/build/outputs/aar/yolokit-release.aar \
  /path/to/android-ndk
```

The check covers all three ABIs, exported symbols, debug sections, diagnostic
strings, linker flags, public API classes, and JNI method descriptors. Also
test model loading and detection on a device before distributing a changed JNI
build. Keep R8 mappings and unstripped native libraries from the build separately
for diagnosing crashes; do not attach them to the public AAR.

## Add to an Android app

Place the AAR in `app/libs/yolokit-release.aar`. The app's Gradle repositories
must include `google()` and `mavenCentral()`.
In the app module's `build.gradle.kts`:

```kotlin
dependencies {
    implementation(files("libs/yolokit-release.aar"))
    implementation("com.qualcomm.qti:qnn-runtime:2.50.0")
}

android {
    packaging {
        jniLibs {
            useLegacyPackaging = true
            excludes += listOf(
                "**/libQnnDsp*.so",
                "**/libQnnGpu.so",
                "**/libQnnHtpPrepare.so",
            )
        }
    }
}
```

A local AAR has no Maven dependency metadata: QNN runtime must be declared
explicitly. For an ncnn-only app, omit that dependency. Kotlin apps normally
already depend on the Kotlin standard library; Java-only apps also need a
compatible Kotlin stdlib dependency.

`useLegacyPackaging = true` makes native libraries available as extracted
files in `applicationInfo.nativeLibraryDir`, which YoloKit includes in
`ADSP_LIBRARY_PATH` for QNN's DSP loader. Keep the JNI consumer rules included
in the AAR when enabling R8 in the app; no package-wide keep rule is needed.

For QNN, declare the device's DSP transport library inside the consuming app's
`<application>` element:

```xml
<uses-native-library
    android:name="libcdsprpc.so"
    android:required="false" />
```

Applications targeting Android 12 or newer must explicitly request access to
vendor native libraries. The optional declaration allows installation on phones
without this library, where the app can use ncnn instead. Without it, QNN may
report `loadRemoteSymbols failed` or error `14001` even when the context file
is compatible. See [Android's manifest documentation](https://developer.android.com/guide/topics/manifest/uses-native-library-element).

## Several Hexagon architectures

The cached QNN 2.50.0 runtime AAR contains HTP Stub/Skel pairs for V68, V69,
V73, V75, V79 and V81. It contains no V61 pair. Do not assume V61 support;
check the actual SoC against Qualcomm's supported-device documentation.

One YoloKit AAR can be used for these supported targets. Package the runtime
libraries for the targets you support and compatible context binaries in the
app's `src/main/assets/`, for example:

```text
models/device_v73.bin
models/device_v81.bin
```

The runtime selects its device backend. The app must select the model asset
prepared for the actual target SoC and runtime SDK version. YoloKit does not
detect the architecture or select a model automatically.

```kotlin
// Set this using the app's validated mapping of supported devices to models.
val modelAsset = "models/device_v81.bin"
val config = ModelConfig.Builder()
    .qnn(modelAsset)
    .labels(myClasses)
    .inputSize(640)
    .build()

// Load and run inference off the UI thread. Handle failure on unsupported devices.
val kit = YoloKit.create(context, config).getOrThrow()
try {
    val detections = kit.detect(bitmap)
} finally {
    kit.close()
}
```

`YoloKit.isQnnAvailable(context)` checks arm64 support, Qualcomm device hints,
and whether the native QNN code was compiled in. It does not validate the DSP
runtime or a particular context binary; model initialization can still fail.

For a V81-only app, optionally exclude the other HTP Stub/Skel pairs from
the APK in the same `jniLibs` block:

```kotlin
excludes += listOf(
    "**/libQnnHtpV68*.so",
    "**/libQnnHtpV69*.so",
    "**/libQnnHtpV73*.so",
    "**/libQnnHtpV75*.so",
    "**/libQnnHtpV79*.so",
)
```

Retain `libQnnHtp.so`, `libQnnSystem.so`, and the V81 Stub/Skel pair.
Do not apply these exclusions to an app intended to run QNN on older targets.

References: [Android library optimization](https://developer.android.com/topic/performance/app-optimization/library-optimization),
[Qualcomm supported devices](https://docs.qualcomm.com/nav/home/QNN_general_overview.html?product=924033590759186372).
