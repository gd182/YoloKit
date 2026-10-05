<h1 align="center">YoloKit</h1>

<p align="center">YOLO object detection for Android.</p>

<div align="center">
  <img src="https://skillicons.dev/icons?i=kotlin" height="40" alt="Kotlin logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=cpp" height="40" alt="C++ logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=cmake" height="40" alt="CMake logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=gradle" height="40" alt="Gradle logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=androidstudio" height="40" alt="Android Studio logo" />
</div>

<p align="center"><b>English</b> · <a href="README.ru.md">Русский</a></p>

YoloKit is an Android library for running YOLO models on bitmaps. It returns
bounding boxes, class names, and confidence scores through a Kotlin API.

It uses ncnn with Vulkan acceleration and CPU fallback, or Qualcomm QNN on
supported Snapdragon devices. The library handles image padding, box decoding,
and NMS in C++; it does not use OpenCV.

The camera sample is in [YoloKitMobile](https://github.com/gd182/YoloKitMobile).

## Add to your project

The library is connected from source through a Gradle composite build.
Add it as a submodule:

```bash
git submodule add https://github.com/gd182/YoloKit.git vendor/YoloKit
```

In `settings.gradle.kts`:

```kotlin
includeBuild("vendor/YoloKit") {
    dependencySubstitution {
        substitute(module("io.github.gd182:yolokit")).using(project(":yolokit"))
    }
}
```

In the app's `build.gradle.kts`:

```kotlin
dependencies {
    implementation("io.github.gd182:yolokit")
}
```

You will need Android Studio, JDK 17, Android SDK 37, the NDK, and CMake 3.22.1.
The project uses AGP 9.4.0. ncnn is included in the repository.

The library supports Android 7.0 and newer and builds for `arm64-v8a`,
`armeabi-v7a`, and `x86_64`. QNN is available only on `arm64-v8a`.

## Run a model

Put the model files in your app's `src/main/assets/` directory.
This example uses a raw YOLOv8 DFL model with COCO labels:

```kotlin
import io.github.gd182.yolokit.ModelConfig
import io.github.gd182.yolokit.YoloKit

val config = ModelConfig.Builder()
    .ncnn(param = "yolov8n.param", bin = "yolov8n.bin")
    .labels(YoloKit.COCO_LABELS)
    .inputSize(640)
    .thresholds(confidence = 0.25f, nms = 0.45f)
    .build()

val kit = YoloKit.create(context, config).getOrThrow()
val detections = kit.detect(bitmap)
```

Load the model and run detection off the UI thread. The input bitmap must be
ARGB_8888. Each result has `rect`, `label`, `labelName`, and `score`;
the rectangle uses the bitmap's pixel coordinates.

Reuse `kit` for subsequent frames and call `kit.close()` when finished.
The native layer holds one model per backend, so close the existing detector
before loading another model. Calls to `detect` are serialized internally.

For an Ultralytics `format=ncnn` export, change the model configuration to:

```kotlin
val config = ModelConfig.Builder()
    .ncnn("model.ncnn.param", "model.ncnn.bin", decoded = true)
    .labels(myClasses)
    .inputSize(640)
    .build()
```

The builder sets the usual `in0` / `out0` tensor names and RGB input order
for this export. Use your own class labels and export input size.

## Model requirements

`inputSize` must match the export's `imgsz` and be a positive multiple of 32.
Labels must match the model's classes in training order.

| Model | Output |
|---|---|
| Raw YOLOv8 DFL, `decoded = false` | One output; YoloKit decodes the boxes |
| Ultralytics ncnn, `decoded = true` | Boxes already decoded in the graph |
| QNN | Decoded `[1, 4+nc, N]` output, without NMS in the graph |

Model weights are not included.

## QNN

To compile QNN support, set the QAIRT SDK path in the library's
`local.properties`:

```properties
qnn.sdk.dir=/path/to/QAIRT/2.50.0
```

You can also use `QNN_SDK_ROOT` or the Gradle property `-Pqnn.sdk.dir=...`.
The SDK version must match `qnn` in `gradle/libs.versions.toml`.
Without the SDK, the library builds with ncnn only.

QNN loads a context binary built for the target device. The sample app has a
[conversion guide](https://github.com/gd182/YoloKitMobile/blob/main/docs/QNN.md)
and a `scripts/build_qnn_model.sh` script.

```kotlin
if (YoloKit.isQnnAvailable(context)) {
    val config = ModelConfig.Builder()
        .qnn("model_v81.bin")
        .labels(myClasses)
        .inputSize(640)
        .build()

    val kit = YoloKit.create(context, config).getOrThrow()
    val detections = kit.detect(bitmap)
    kit.close()
}
```

The QNN runtime is a Gradle dependency, so its libraries are packaged in the
APK even when you use only ncnn. The consuming app also needs the packaging
exclusions used by YoloKit:

```kotlin
android {
    packaging {
        jniLibs {
            excludes += listOf(
                "**/libQnnDsp*.so",
                "**/libQnnGpu.so",
                "**/libQnnHtpPrepare.so",
            )
        }
    }
}
```

## Source

The Kotlin API is in `yolokit/src/main/java/io/github/gd182/yolokit/YoloKit.kt`.
Native code and the bundled ncnn build are in `yolokit/src/main/cpp/`.
The project uses C++17 and ncnn 20260526.


## Architecture

<p align="center"><img src="docs/stack.svg" alt="YoloKit stack" width="720"/></p>

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for the terms covering
ncnn and the QNN runtime. Model weights have their own licenses.
