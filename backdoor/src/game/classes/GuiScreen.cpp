#include "pch.h"
#include "GuiScreen.h"

#include "../jni/Class.h"
#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

bool GuiScreen::IsInventory(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return false;
    }

    const std::string className = Mapper::Get("net/minecraft/client/gui/inventory/GuiInventory");
    if (className.empty()) {
        return false;
    }

    Class* inventoryClass = g_Game->FindClass(className);
    return inventoryClass && env->IsInstanceOf(reinterpret_cast<jobject>(this), reinterpret_cast<jclass>(inventoryClass));
}
