#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/bitmap.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "qnn_yolo.h"
#include "jni_registration.h"

static std::unique_ptr<QnnYolo> g_qnn;
static std::mutex g_qnn_lock;
static std::vector<Object> g_objects;

static jboolean JNICALL qnn_IsBuilt(JNIEnv*, jobject)
{
#ifdef HAVE_QNN
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

static jstring JNICALL qnn_InitAsset(
        JNIEnv* env, jobject, jobject assetManager, jstring modelAsset, jstring nativeLibDir,
        jint numClass, jboolean boxesNormalized)
{
    std::lock_guard<std::mutex> lk(g_qnn_lock);

    const char* model = env->GetStringUTFChars(modelAsset, nullptr);
    const char* libDir = env->GetStringUTFChars(nativeLibDir, nullptr);
    std::string modelStr(model);
    std::string libDirStr(libDir);
    env->ReleaseStringUTFChars(modelAsset, model);
    env->ReleaseStringUTFChars(nativeLibDir, libDir);

    g_qnn.reset(new QnnYolo());
    std::string err = g_qnn->load(AAssetManager_fromJava(env, assetManager), modelStr, libDirStr,
                                  numClass, boxesNormalized == JNI_TRUE);
    if (!err.empty()) g_qnn.reset();
    return env->NewStringUTF(err.c_str());
}

static jstring JNICALL qnn_InitBytes(
        JNIEnv* env, jobject, jbyteArray binary, jstring nativeLibDir,
        jint numClass, jboolean boxesNormalized)
{
    std::lock_guard<std::mutex> lk(g_qnn_lock);

    // One copy from the Java array; the QNN loader reads from this buffer.
    const jsize n = env->GetArrayLength(binary);
    std::vector<uint8_t> bytes((size_t)n);
    if (n > 0)
        env->GetByteArrayRegion(binary, 0, n, reinterpret_cast<jbyte*>(bytes.data()));

    const char* libDir = env->GetStringUTFChars(nativeLibDir, nullptr);
    std::string libDirStr(libDir);
    env->ReleaseStringUTFChars(nativeLibDir, libDir);

    g_qnn.reset(new QnnYolo());
    std::string err = g_qnn->load(bytes, libDirStr, numClass, boxesNormalized == JNI_TRUE);
    if (!err.empty()) g_qnn.reset();
    return env->NewStringUTF(err.c_str());
}

static jstring JNICALL qnn_Describe(JNIEnv* env, jobject)
{
    std::lock_guard<std::mutex> lk(g_qnn_lock);
    std::string s = g_qnn ? g_qnn->describe() : std::string();
    return env->NewStringUTF(s.c_str());
}

static void JNICALL qnn_Release(JNIEnv*, jobject)
{
    std::lock_guard<std::mutex> lk(g_qnn_lock);
    g_qnn.reset();
}

static jfloatArray JNICALL qnn_Detect(
        JNIEnv* env, jobject, jobject bitmap, jfloat probThreshold, jfloat nmsThreshold)
{
    std::lock_guard<std::mutex> lk(g_qnn_lock);
    if (!g_qnn) return env->NewFloatArray(0);

    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
        return env->NewFloatArray(0);

    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
        return env->NewFloatArray(0);

    g_objects.clear();
    g_qnn->detect((const unsigned char*)pixels, (int)info.width, (int)info.height, g_objects,
                  probThreshold, nmsThreshold);
    AndroidBitmap_unlockPixels(env, bitmap);

    std::vector<float> buf(g_objects.size() * 6);
    for (size_t i = 0; i < g_objects.size(); i++)
    {
        buf[i * 6 + 0] = g_objects[i].x;
        buf[i * 6 + 1] = g_objects[i].y;
        buf[i * 6 + 2] = g_objects[i].w;
        buf[i * 6 + 3] = g_objects[i].h;
        buf[i * 6 + 4] = (float)g_objects[i].label;
        buf[i * 6 + 5] = g_objects[i].prob;
    }
    jfloatArray result = env->NewFloatArray((jsize)buf.size());
    env->SetFloatArrayRegion(result, 0, (jsize)buf.size(), buf.data());
    return result;
}

bool register_qnn_natives(JNIEnv* env)
{
    const JNINativeMethod methods[] = {
        native_method("nativeIsBuilt", "()Z", qnn_IsBuilt),
        native_method("nativeInitAsset",
                      "(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/lang/String;IZ)Ljava/lang/String;",
                      qnn_InitAsset),
        native_method("nativeInitBytes", "([BLjava/lang/String;IZ)Ljava/lang/String;",
                      qnn_InitBytes),
        native_method("nativeDescribe", "()Ljava/lang/String;", qnn_Describe),
        native_method("nativeRelease", "()V", qnn_Release),
        native_method("nativeDetect", "(Landroid/graphics/Bitmap;FF)[F", qnn_Detect),
    };
    return register_native_methods(env, "io/github/gd182/yolokit/internal/QnnNative",
                                   methods, sizeof(methods) / sizeof(methods[0]));
}
