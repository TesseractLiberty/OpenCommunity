#include "pch.h"
#include "PacketClientStatus.h"

#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

jobject PacketClientStatus::Create(JNIEnv* env, int stateOrdinal) {
    if (!env || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string packetClassName = Mapper::Get("net/minecraft/network/play/client/C16PacketClientStatus");
    const std::string enumClassName = Mapper::Get("net/minecraft/network/play/client/C16PacketClientStatus$EnumState");
    if (packetClassName.empty() || enumClassName.empty()) {
        return nullptr;
    }

    jclass packetClass = reinterpret_cast<jclass>(g_Game->FindClass(packetClassName));
    jclass enumClass = reinterpret_cast<jclass>(g_Game->FindClass(enumClassName));
    if (!packetClass || !enumClass) {
        return nullptr;
    }

    const std::string valuesSignature = "()[L" + enumClassName + ";";
    jmethodID valuesMethod = env->GetStaticMethodID(enumClass, "values", valuesSignature.c_str());
    if (!valuesMethod) {
        return nullptr;
    }

    jobjectArray values = static_cast<jobjectArray>(env->CallStaticObjectMethod(enumClass, valuesMethod));
    if (!values) {
        return nullptr;
    }

    const jsize valueCount = env->GetArrayLength(values);
    if (stateOrdinal < 0 || stateOrdinal >= valueCount) {
        env->DeleteLocalRef(values);
        return nullptr;
    }

    jobject state = env->GetObjectArrayElement(values, stateOrdinal);
    env->DeleteLocalRef(values);
    if (!state) {
        return nullptr;
    }

    const std::string constructorSignature = "(L" + enumClassName + ";)V";
    jmethodID constructor = env->GetMethodID(packetClass, "<init>", constructorSignature.c_str());
    if (!constructor) {
        env->DeleteLocalRef(state);
        return nullptr;
    }

    jobject packet = env->NewObject(packetClass, constructor, state);
    env->DeleteLocalRef(state);
    return packet;
}
