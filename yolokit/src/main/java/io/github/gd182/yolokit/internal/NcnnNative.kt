package io.github.gd182.yolokit.internal

import android.content.res.AssetManager
import android.graphics.Bitmap

internal object NcnnNative {
    init {
        System.loadLibrary("yolokit")
    }

    external fun nativeHasGpu(): Boolean

    external fun nativeInit(
        assetManager: AssetManager,
        paramAsset: String,
        binAsset: String,
        inputName: String,
        outputName: String,
        targetSize: Int,
        numClass: Int,
        decoded: Boolean,
        bgr: Boolean,
        useGpu: Boolean,
    ): Boolean

    external fun nativeDetect(bitmap: Bitmap, probThreshold: Float, nmsThreshold: Float): FloatArray

    external fun nativeRelease()

    /** Destroys the Vulkan instance. Call only after [nativeRelease]; a no-op while a model is loaded. */
    external fun nativeReleaseGpu()

    external fun nativePrecision(): String
}

internal object QnnNative {
    init {
        System.loadLibrary("yolokit")
    }

    external fun nativeIsBuilt(): Boolean

    /** Returns an empty string on success, otherwise the reason. */
    external fun nativeInit(
        assetManager: AssetManager,
        modelAsset: String,
        nativeLibDir: String,
        numClass: Int,
        boxesNormalized: Boolean,
    ): String

    external fun nativeDescribe(): String

    external fun nativeRelease()

    external fun nativeDetect(bitmap: Bitmap, probThreshold: Float, nmsThreshold: Float): FloatArray
}
