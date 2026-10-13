# Сборка AAR и подключение QNN

[English](AAR.md) · **Русский**

Релизы YoloKit будут содержать AAR без весов моделей. Опубликованные сборки
доступны в [Releases](https://github.com/gd182/YoloKit/releases).
AAR также можно собрать из исходников по инструкции ниже.
API и управление детектором описаны в [README библиотеки](../README.ru.md).

## Сборка

Для поддержки QNN укажите путь к QAIRT 2.50.0 в `local.properties` библиотеки
через `qnn.sdk.dir` или передайте Gradle
`-Pqnn.sdk.dir=/path/to/QAIRT/2.50.0`.
Без заголовков SDK нативная библиотека поддерживает только ncnn.

```bash
./gradlew :yolokit:assembleRelease
```

Результат: `yolokit/build/outputs/aar/yolokit-release.aar`.

В release-сборке R8 обрабатывает внутренние JVM-классы. Имена публичного API
и двух классов JNI сохраняются. Обфускация не затрагивает C++-код, файлы моделей
и нативные библиотеки Qualcomm.

Сохраняйте `yolokit/build/outputs/mapping/release/mapping.txt` для каждого
выпуска: он нужен для расшифровки стектрейсов. Приложению, которое подключает
AAR, этот файл не требуется.

## Защита release-сборки

Нативные методы регистрируются через `RegisterNatives`. Библиотека экспортирует
только `JNI_OnLoad` и `JNI_OnUnload`; внутренние C++-символы скрывает
скрипт линковщика. Из release AAR удаляются отладочные символы.
Подробные логи тензоров и времени обработки включаются только в debug-сборке;
сообщения об ошибках остаются.

R8 сохраняет публичный API и имена JNI-классов и методов, необходимые для
регистрации. Остальные внутренние члены можно удалить или обфусцировать.
Это усложняет анализ AAR, но не шифрует нативный код или файлы моделей.

Для проверки собранного release AAR используйте скрипт. Нужны macOS или Linux,
JDK и инструменты Android NDK:

```bash
bash scripts/check_release_aar.sh \
  yolokit/build/outputs/aar/yolokit-release.aar \
  /path/to/android-ndk
```

Проверяются все три ABI, экспортируемые символы, отладочные секции, строки
диагностики, параметры линковщика, классы публичного API и сигнатуры JNI.
Перед распространением версии с изменённым JNI также проверьте загрузку модели
и распознавание на устройстве. Файлы R8 mapping и нативные библиотеки до удаления
отладочных символов храните отдельно для диагностики; не прикладывайте их
к публичному AAR.

## Подключение к приложению

Положите AAR в `app/libs/yolokit-release.aar`.
В репозиториях зависимостей Gradle должны быть `google()` и `mavenCentral()`.
В `build.gradle.kts` модуля приложения добавьте:

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

У локального AAR нет метаданных зависимостей Maven, поэтому QNN runtime
подключается отдельно. Для приложения только с ncnn эту зависимость можно
не добавлять. В Kotlin-приложениях стандартная библиотека Kotlin обычно уже
подключена; приложениям только на Java понадобится совместимая зависимость
Kotlin stdlib.

`useLegacyPackaging = true` позволяет загрузить нативные библиотеки как
распакованные файлы из `applicationInfo.nativeLibraryDir`.
YoloKit добавляет этот каталог в `ADSP_LIBRARY_PATH` для загрузчика DSP.
При включении R8 в приложении сохраните правила JNI, включённые в AAR;
правило сохранения всего пакета не требуется.

Для QNN объявите библиотеку доступа к DSP внутри элемента `<application>`
манифеста приложения:

```xml
<uses-native-library
    android:name="libcdsprpc.so"
    android:required="false" />
```

При целевой версии Android 12 и новее доступ к нативным библиотекам производителя
нужно запрашивать явно. `required="false"` позволяет установить приложение
на телефон без этой библиотеки и использовать ncnn.
Без объявления QNN может вернуть `loadRemoteSymbols failed` или ошибку
`14001`, даже если файл контекста подходит устройству.
Подробнее — в [документации манифеста Android](https://developer.android.com/guide/topics/manifest/uses-native-library-element).

## Разные архитектуры Hexagon

В AAR QNN runtime 2.50.0 есть пары библиотек HTP Stub/Skel для V68, V69,
V73, V75, V79 и V81. Пары V61 нет. Совместимость конкретного SoC проверяйте
по [документации Qualcomm](https://docs.qualcomm.com/nav/home/QNN_general_overview.html?product=924033590759186372).

Один AAR YoloKit можно использовать для этих поддерживаемых архитектур.
В приложение должны входить подходящие библиотеки runtime и файлы контекста,
подготовленные для целевых устройств, например:

```text
models/device_v73.bin
models/device_v81.bin
```

Runtime выбирает бэкенд устройства. Приложение само выбирает файл модели
под конкретный SoC и версию SDK. YoloKit не определяет архитектуру HTP
и не выбирает модель автоматически.

```kotlin
// Выберите файл по проверенному соответствию устройств и моделей в приложении.
val modelAsset = "models/device_v81.bin"
val config = ModelConfig.Builder()
    .qnn(modelAsset)
    .labels(myClasses)
    .inputSize(640)
    .build()

// Загружайте модель вне потока интерфейса и обработайте ошибку загрузки.
val kit = YoloKit.create(context, config).getOrThrow()
try {
    val detections = kit.detect(bitmap)
} finally {
    kit.close()
}
```

`YoloKit.isQnnAvailable(context)` проверяет поддержку arm64, признаки устройства
Qualcomm и наличие QNN в нативной сборке. Метод не проверяет DSP runtime
и конкретный файл контекста. Инициализация модели всё равно может завершиться
с ошибкой.

Если приложение рассчитано только на V81, можно исключить остальные пары
HTP Stub/Skel в том же блоке `jniLibs`:

```kotlin
excludes += listOf(
    "**/libQnnHtpV68*.so",
    "**/libQnnHtpV69*.so",
    "**/libQnnHtpV73*.so",
    "**/libQnnHtpV75*.so",
    "**/libQnnHtpV79*.so",
)
```

Оставьте `libQnnHtp.so`, `libQnnSystem.so` и пару V81 Stub/Skel.
Не используйте эти исключения, если QNN должен работать и на более старых
устройствах.

## Ссылки

- [README YoloKit](../README.ru.md)
- [Приложение-пример YoloKitMobile](https://github.com/gd182/YoloKitMobile)
- [Конвертация моделей для QNN](https://github.com/gd182/YoloKitMobile/blob/main/docs/QNN.ru.md)
- [Оптимизация Android-библиотек](https://developer.android.com/topic/performance/app-optimization/library-optimization)
- [Документация Qualcomm](https://docs.qualcomm.com/nav/home/QNN_general_overview.html?product=924033590759186372)
