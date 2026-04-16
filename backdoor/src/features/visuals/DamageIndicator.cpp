#include "pch.h"
#include "DamageIndicator.h"

#include "../../game/jni/Class.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/mapping/Mapper.h"

#include <gl/GL.h>
#include <cfloat>

namespace {
    struct ArmorRenderData {
        jobject stack = nullptr;
        ImVec2 position{};
    };

    ImU32 GetPercentageColor(float percent) {
        if (percent >= 0.70f) {
            return IM_COL32(102, 214, 112, 255);
        }
        if (percent >= 0.50f) {
            return IM_COL32(255, 219, 97, 255);
        }
        if (percent >= 0.20f) {
            return IM_COL32(255, 167, 72, 255);
        }
        return IM_COL32(255, 98, 98, 255);
    }
}

void DamageIndicator::RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
    if (!IsEnabled() || !drawList || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    JNIEnv* env = g_Game->GetCurrentEnv();
    if (!env || env->PushLocalFrame(96) != 0) {
        return;
    }

    jobject currentScreenObject = Minecraft::GetCurrentScreen(env);
    if (currentScreenObject) {
        auto* screen = reinterpret_cast<GuiScreen*>(currentScreenObject);
        if (screen->IsInventory(env)) {
            env->PopLocalFrame(nullptr);
            return;
        }
    }

    jobject mouseOverObject = Minecraft::GetObjectMouseOver(env);
    if (!mouseOverObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject);
    if (!mouseOver->IsAimingEntity(env)) {
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject entityObject = mouseOver->GetEntity(env);
    if (!entityObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    const std::string playerClassName = Mapper::Get("net/minecraft/entity/player/EntityPlayer");
    Class* playerClass = playerClassName.empty() ? nullptr : g_Game->FindClass(playerClassName);
    if (!playerClass || !env->IsInstanceOf(entityObject, reinterpret_cast<jclass>(playerClass))) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* target = reinterpret_cast<Player*>(entityObject);
    const std::string name = target->GetName(env, true);
    const float realHealth = target->GetRealHealth(env);
    if (name.empty() || realHealth < 0.0f) {
        env->PopLocalFrame(nullptr);
        return;
    }

    const int health = static_cast<int>(std::roundf(realHealth));
    int maxHealth = static_cast<int>(std::roundf(target->GetMaxHealth(env)));
    if (maxHealth <= 0) {
        maxHealth = 20;
    }

    const float scale = GetScale();
    MarkInUse(100);
    const float width = 250.0f * scale;
    const float height = 92.0f * scale;
    const ImVec2 origin(
        GetAnchorX() * screenW - (width * 0.5f),
        GetAnchorY() * screenH - (height * 0.5f));

    const float lineHeight = 18.0f * scale;
    const ImU32 shadowColor = IM_COL32(0, 0, 0, 180);
    const ImU32 textColor = IM_COL32(255, 255, 255, 255);
    const ImU32 healthColor = GetPercentageColor((std::clamp)(static_cast<float>(health) / static_cast<float>(maxHealth), 0.0f, 1.0f));

    const std::string headerText = "[" + name + "]";
    const ImVec2 namePos(origin.x, origin.y);
    drawList->AddText(nullptr, 16.0f * scale, ImVec2(namePos.x + 1.0f, namePos.y + 1.0f), shadowColor, headerText.c_str());
    drawList->AddText(nullptr, 16.0f * scale, namePos, textColor, headerText.c_str());

    char healthText[32];
    std::snprintf(healthText, sizeof(healthText), "%d", health);
    const ImVec2 healthPos(origin.x, origin.y + lineHeight);
    drawList->AddText(nullptr, 18.0f * scale, ImVec2(healthPos.x + 1.0f, healthPos.y + 1.0f), shadowColor, healthText);
    drawList->AddText(nullptr, 18.0f * scale, healthPos, healthColor, healthText);

    const ImVec2 healthTextSize = ImGui::GetFont()->CalcTextSizeA(18.0f * scale, FLT_MAX, 0.0f, healthText);
    const ImVec2 heartPos(origin.x + healthTextSize.x + 5.0f * scale, origin.y + lineHeight + 2.0f * scale);
    drawList->AddText(nullptr, 18.0f * scale, ImVec2(heartPos.x + 1.0f, heartPos.y + 1.0f), shadowColor, ICON_MD_FAVORITE);
    drawList->AddText(nullptr, 18.0f * scale, heartPos, IM_COL32(255, 50, 50, 255), ICON_MD_FAVORITE);

    std::vector<ArmorRenderData> armorItems;
    armorItems.reserve(4);

    float armorY = origin.y + (lineHeight * 2.0f);
    for (int index = 3; index >= 0; --index) {
        jobject armorObject = target->GetCurrentArmor(index, env);
        if (!armorObject) {
            armorY += lineHeight;
            continue;
        }

        auto* armor = reinterpret_cast<ItemStack*>(armorObject);
        armorItems.push_back({ armorObject, ImVec2(origin.x, armorY) });

        const int maxDamage = armor->GetMaxDamage(env);
        if (maxDamage > 0) {
            const int currentDurability = maxDamage - armor->GetItemDamage(env);
            const float percent = (std::clamp)(static_cast<float>(currentDurability) / static_cast<float>(maxDamage), 0.0f, 1.0f);

            char durabilityText[32];
            std::snprintf(durabilityText, sizeof(durabilityText), "(%d)", currentDurability);
            const ImVec2 durabilityPos(origin.x + 20.0f * scale, armorY + 1.0f * scale);
            drawList->AddText(nullptr, 14.0f * scale, ImVec2(durabilityPos.x + 1.0f, durabilityPos.y + 1.0f), shadowColor, durabilityText);
            drawList->AddText(nullptr, 14.0f * scale, durabilityPos, GetPercentageColor(percent), durabilityText);
        }

        armorY += lineHeight;
    }

    if (!armorItems.empty()) {
        jobject renderItemObject = Minecraft::GetRenderItem(env);
        if (renderItemObject) {
            auto* renderItem = reinterpret_cast<RenderItem*>(renderItemObject);

            glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            GLint viewport[4];
            glGetIntegerv(GL_VIEWPORT, viewport);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, viewport[2], viewport[3], 0.0, -1000.0, 1000.0);

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_LIGHTING);
            glEnable(GL_COLOR_MATERIAL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            RenderHelper::EnableGUIStandardItemLighting(env);

            for (const auto& armorItem : armorItems) {
                glPushMatrix();
                glTranslatef(armorItem.position.x, armorItem.position.y, 0.0f);
                glScalef(scale, scale, 1.0f);
                renderItem->RenderItemIntoGUI(armorItem.stack, 0, 0, env);
                glPopMatrix();
            }

            RenderHelper::DisableStandardItemLighting(env);

            glDisable(GL_BLEND);
            glDisable(GL_COLOR_MATERIAL);
            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);

            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();
            glPopAttrib();
        }
    }

    env->PopLocalFrame(nullptr);
}
