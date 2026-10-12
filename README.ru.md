<h1 align="center">YoloKit</h1>

<p align="center">Распознавание объектов с помощью YOLO на Android.</p>

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

<p align="center"><a href="README.md">English</a> · <b>Русский</b></p>

YoloKit — Android-библиотека для запуска моделей YOLO. Она принимает Bitmap
и возвращает координаты объектов, названия классов и оценки уверенности.

Модель работает через ncnn с Vulkan или на CPU. На поддерживаемых устройствах
Snapdragon можно использовать Qualcomm QNN и NPU Hexagon. Подготовка изображения,
декодирование координат и NMS выполняются на C++, без OpenCV.

Пример с камерой: [YoloKitMobile](https://github.com/gd182/YoloKitMobile).

## Подключение

Готовый AAR с обфускацией можно собрать командой
`./gradlew :yolokit:assembleRelease`. Файл появится в
`yolokit/build/outputs/aar/yolokit-release.aar`.
Инструкция по подключению AAR, QNN runtime и выбору модели:
[docs/AAR.md](docs/AAR.md).

Библиотека подключается из исходников через Gradle composite build.
Добавьте её как подмодуль:

```bash
git submodule add https://github.com/gd182/YoloKit.git vendor/YoloKit
```

В `settings.gradle.kts`:

```kotlin
includeBuild("vendor/YoloKit") {
    dependencySubstitution {
        substitute(module("io.github.gd182:yolokit")).using(project(":yolokit"))
    }
}
```

В `build.gradle.kts` приложения:

```kotlin
dependencies {
    implementation("io.github.gd182:yolokit")
}
```

Для сборки нужны Android Studio, JDK 17, Android SDK 37, NDK и CMake 3.22.1.
Проект использует AGP 9.4.0. Готовая сборка ncnn уже есть в репозитории.

Библиотека работает на Android 7.0 и новее. Поддерживаются `arm64-v8a`,
`armeabi-v7a` и `x86_64`; QNN доступен только на `arm64-v8a`.

## Запуск модели

Положите файлы модели в `src/main/assets/` вашего приложения.
Пример для YOLOv8 с выходом DFL и классами COCO:

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

Загружайте модель и выполняйте распознавание вне потока интерфейса.
Bitmap должен иметь формат ARGB_8888. У каждого результата есть `rect`,
`label`, `labelName` и `score`. Координаты `rect` заданы в пикселях
исходного Bitmap.

Используйте тот же `kit` для следующих кадров, а после работы вызовите
`kit.close()`. Нативный код хранит одну модель на бэкенд, поэтому перед
загрузкой другой модели нужно закрыть текущий детектор.
Вызовы `detect` выполняются последовательно.

Для экспорта Ultralytics `format=ncnn` конфигурация выглядит так:

```kotlin
val config = ModelConfig.Builder()
    .ncnn("model.ncnn.param", "model.ncnn.bin", decoded = true)
    .labels(myClasses)
    .inputSize(640)
    .build()
```

Для этого экспорта builder сам задаёт имена тензоров `in0` / `out0`
и порядок цветов RGB. Укажите свои классы и размер входа при экспорте.

## Требования к модели

`inputSize` должен совпадать с `imgsz` при экспорте и быть положительным числом,
кратным 32. Названия в `labels` должны соответствовать классам модели
и идти в порядке обучения.

| Модель | Выход |
|---|---|
| YOLOv8 DFL, `decoded = false` | Один выход; координаты декодирует YoloKit |
| Ultralytics ncnn, `decoded = true` | Координаты уже декодированы в графе |
| QNN | Декодированный выход `[1, 4+nc, N]`, без NMS в графе |

Весов моделей в библиотеке нет.

## Настройка QNN

Для сборки с QNN укажите путь к QAIRT SDK в `local.properties` библиотеки:

```properties
qnn.sdk.dir=/path/to/QAIRT/2.50.0
```

Вместо этого можно использовать `QNN_SDK_ROOT` или параметр Gradle
`-Pqnn.sdk.dir=...`. Версия SDK должна совпадать с `qnn` в
`gradle/libs.versions.toml`. Без SDK библиотека собирается только с ncnn.

QNN загружает файл контекста, собранный под целевое устройство. В приложении-примере
есть [инструкция по конвертации](https://github.com/gd182/YoloKitMobile/blob/main/docs/QNN.ru.md)
и скрипт `scripts/build_qnn_model.sh`.

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

QNN runtime подключён как зависимость Gradle. Его библиотеки попадают в APK,
даже если используется только ncnn. В приложении нужно повторить исключения
из настроек упаковки YoloKit:

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

## Исходники

Kotlin API находится в `yolokit/src/main/java/io/github/gd182/yolokit/YoloKit.kt`.
Нативный код и готовая сборка ncnn — в `yolokit/src/main/cpp/`.
В проекте используются C++17 и ncnn 20260526.


## Архитектура

<p align="center"><img src="docs/stack.svg" alt="Архитектура YoloKit" width="720"/></p>

## Лицензия

Apache-2.0. Условия для ncnn и QNN runtime приведены в
[LICENSE](LICENSE) и [NOTICE](NOTICE). У весов моделей свои лицензии.
