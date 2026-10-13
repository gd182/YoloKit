#pragma once

#include <jni.h>

template <typename Function>
inline JNINativeMethod native_method(const char* name, const char* signature, Function function)
{
    return {const_cast<char*>(name), const_cast<char*>(signature),
            reinterpret_cast<void*>(function)};
}

inline bool register_native_methods(JNIEnv* env, const char* class_name,
                                    const JNINativeMethod* methods, jint count)
{
    jclass type = env->FindClass(class_name);
    if (!type) return false;
    const jint result = env->RegisterNatives(type, methods, count);
    env->DeleteLocalRef(type);
    return result == JNI_OK;
}

bool register_ncnn_natives(JNIEnv* env);
bool register_qnn_natives(JNIEnv* env);
