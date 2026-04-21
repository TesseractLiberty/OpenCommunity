#include "pch.h"
#include "RenderItem.h"

#include "../jni/Class.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

void RenderItem::RenderItemIntoGUI(jobject itemStack, int x, int y, JNIEnv* env) {
    if (!env || !this || !itemStack || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    const std::string className = Mapper::Get("net/minecraft/client/renderer/entity/RenderItem");
    const std::string methodName = Mapper::Get("renderItemIntoGUI");
    const std::string signature = "(" + Mapper::Get("net/minecraft/item/ItemStack", 2) + "II)V";
    if (className.empty() || methodName.empty() || signature.empty()) {
        return;
    }

    Class* renderItemClass = g_Game->FindClass(className);
    Method* method = renderItemClass ? renderItemClass->GetMethod(env, methodName.c_str(), signature.c_str()) : nullptr;
    if (method) {
        method->CallVoidMethod(env, this, false, itemStack, x, y);
    }
}
