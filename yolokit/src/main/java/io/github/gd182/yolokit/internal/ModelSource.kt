package io.github.gd182.yolokit.internal

import android.content.Context

/** Where a model file comes from: an asset path (read natively, no copy) or bytes the app already has. */
internal sealed interface ModelSource {
    class Asset(val name: String) : ModelSource

    /** The array is referenced, not copied, until the native load. Keep it unchanged while loading. */
    class Bytes(val data: ByteArray) : ModelSource
}

/**
 * Returns the file's bytes. Used only when a model mixes asset and byte sources; a model whose
 * files are both assets is loaded natively without this copy.
 */
internal fun ModelSource.readBytes(context: Context): Result<ByteArray> = when (this) {
    is ModelSource.Asset -> runCatching { context.assets.open(name).use { it.readBytes() } }
    is ModelSource.Bytes -> Result.success(data)
}
