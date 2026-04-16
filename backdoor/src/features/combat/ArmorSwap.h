#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/armor_swap_icon.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <string>

#ifdef _BACKDOOR
#include "../../game/classes/Container.h"
#include "../../game/classes/GuiScreen.h"
#include "../../game/classes/ItemArmor.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/PacketClientStatus.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/PlayerController.h"
#include "../../game/classes/Slot.h"
#endif

class ArmorSwap : public Module {
private:
    enum OptionIndex {
        OptionDelay = 0,
        OptionSwapAll,
        OptionPercentage,
        OptionHelmetPct,
        OptionChestPct,
        OptionLegsPct,
        OptionBootsPct,
        OptionAutoDrop,
        OptionMultiSwap,
        OptionOpenInventory,
        OptionInventoryOrganizer,
        OptionHelmetSlot,
        OptionChestSlot,
        OptionLegsSlot,
        OptionBootsSlot,
        OptionCount
    };

    enum class State {
        Idle,
        Opening,
        Processing,
        Organizing,
        Closing
    };

    bool HasOptionSet() const { return m_Options.size() >= OptionCount; }
    int GetDelay() const { return HasOptionSet() ? m_Options[OptionDelay].intValue : 80; }
    bool IsSwapAllEnabled() const { return HasOptionSet() && m_Options[OptionSwapAll].boolValue; }
    int GetPercentage() const { return HasOptionSet() ? m_Options[OptionPercentage].intValue : 25; }
    int GetHelmetPct() const { return HasOptionSet() ? m_Options[OptionHelmetPct].intValue : 25; }
    int GetChestPct() const { return HasOptionSet() ? m_Options[OptionChestPct].intValue : 25; }
    int GetLegsPct() const { return HasOptionSet() ? m_Options[OptionLegsPct].intValue : 25; }
    int GetBootsPct() const { return HasOptionSet() ? m_Options[OptionBootsPct].intValue : 25; }
    bool IsAutoDropEnabled() const { return HasOptionSet() && m_Options[OptionAutoDrop].boolValue; }
    bool IsMultiSwapEnabled() const { return HasOptionSet() && m_Options[OptionMultiSwap].boolValue; }
    bool IsInventoryOrganizerEnabled() const { return HasOptionSet() && m_Options[OptionInventoryOrganizer].boolValue; }
    int GetHelmetSlot() const { return HasOptionSet() ? m_Options[OptionHelmetSlot].intValue : 1; }
    int GetChestSlot() const { return HasOptionSet() ? m_Options[OptionChestSlot].intValue : 2; }
    int GetLegsSlot() const { return HasOptionSet() ? m_Options[OptionLegsSlot].intValue : 3; }
    int GetBootsSlot() const { return HasOptionSet() ? m_Options[OptionBootsSlot].intValue : 4; }

    State m_State = State::Idle;
    std::string m_StatusMsg;
    std::chrono::steady_clock::time_point m_StatusTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);

#ifdef _BACKDOOR
    bool m_ToReplace[4] = { false, false, false, false };
    std::deque<int> m_PendingDrops;
    std::chrono::steady_clock::time_point m_LastActionTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_LastCloseTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::chrono::steady_clock::time_point m_FinishTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    void ResetWorkState(JNIEnv* env);
    void OrganizeInventory(jobject playerObject, JNIEnv* env);
    int FindBestInRange(int armorType, jobject current, jobject playerObject, int startSlot, int endSlot, JNIEnv* env);
    int GetDurabilityPct(jobject stackObject, JNIEnv* env);
    long GetArmorValue(jobject stackObject, JNIEnv* env);
    jobject GetEquippedByType(int armorType, jobject playerObject, JNIEnv* env);
    int FindBestReplacementSlot(int armorType, jobject current, jobject playerObject, JNIEnv* env);
#endif

public:
    MODULE_INFO(ArmorSwap, "ArmorSwap", "Automatically swaps damaged armor with a better set.", ModuleCategory::Combat) {
        SetImagePrefix(module_icons::armor_swap_icon_data, module_icons::armor_swap_icon_data_size);
        AddOption(ModuleOption::SliderInt("Delay", 80, 1, 500));
        AddOption(ModuleOption::Toggle("Swap All", false));
        AddOption(ModuleOption::SliderInt("Percentage", 25, 0, 100));
        AddOption(ModuleOption::SliderInt("Helmet", 25, 0, 100));
        AddOption(ModuleOption::SliderInt("Chest", 25, 0, 100));
        AddOption(ModuleOption::SliderInt("Legs", 25, 0, 100));
        AddOption(ModuleOption::SliderInt("Boots", 25, 0, 100));
        AddOption(ModuleOption::Toggle("Auto Drop", false));
        AddOption(ModuleOption::Toggle("Multi Swap", false));
        AddOption(ModuleOption::Toggle("Open Inventory", true));
        AddOption(ModuleOption::Toggle("Inventory Organizer", false));
        AddOption(ModuleOption::SliderInt("Helmet Slot", 1, 1, 9));
        AddOption(ModuleOption::SliderInt("Chest Slot", 2, 1, 9));
        AddOption(ModuleOption::SliderInt("Legs Slot", 3, 1, 9));
        AddOption(ModuleOption::SliderInt("Boots Slot", 4, 1, 9));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config || !HasOptionSet()) {
            return;
        }

        config->ArmorSwap.m_Enabled = IsEnabled();
        config->ArmorSwap.m_Delay = GetDelay();
        config->ArmorSwap.m_SwapAll = IsSwapAllEnabled();
        config->ArmorSwap.m_Percentage = GetPercentage();
        config->ArmorSwap.m_HelmetPct = GetHelmetPct();
        config->ArmorSwap.m_ChestPct = GetChestPct();
        config->ArmorSwap.m_LegsPct = GetLegsPct();
        config->ArmorSwap.m_BootsPct = GetBootsPct();
        config->ArmorSwap.m_AutoDrop = IsAutoDropEnabled();
        config->ArmorSwap.m_MultiSwap = IsMultiSwapEnabled();
        config->ArmorSwap.m_OpenInventory = true;
        config->ArmorSwap.m_InventoryOrganizer = IsInventoryOrganizerEnabled();
        config->ArmorSwap.m_HelmetSlot = GetHelmetSlot();
        config->ArmorSwap.m_ChestSlot = GetChestSlot();
        config->ArmorSwap.m_LegsSlot = GetLegsSlot();
        config->ArmorSwap.m_BootsSlot = GetBootsSlot();
        config->Modules.m_ArmorSwap = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config || !HasOptionSet()) {
            return;
        }

        SetEnabled(config->ArmorSwap.m_Enabled);
        m_Options[OptionDelay].intValue = config->ArmorSwap.m_Delay;
        m_Options[OptionSwapAll].boolValue = config->ArmorSwap.m_SwapAll;
        m_Options[OptionPercentage].intValue = config->ArmorSwap.m_Percentage;
        m_Options[OptionHelmetPct].intValue = config->ArmorSwap.m_HelmetPct;
        m_Options[OptionChestPct].intValue = config->ArmorSwap.m_ChestPct;
        m_Options[OptionLegsPct].intValue = config->ArmorSwap.m_LegsPct;
        m_Options[OptionBootsPct].intValue = config->ArmorSwap.m_BootsPct;
        m_Options[OptionAutoDrop].boolValue = config->ArmorSwap.m_AutoDrop;
        m_Options[OptionMultiSwap].boolValue = config->ArmorSwap.m_MultiSwap;
        m_Options[OptionOpenInventory].boolValue = true;
        m_Options[OptionInventoryOrganizer].boolValue = config->ArmorSwap.m_InventoryOrganizer;
        m_Options[OptionHelmetSlot].intValue = config->ArmorSwap.m_HelmetSlot;
        m_Options[OptionChestSlot].intValue = config->ArmorSwap.m_ChestSlot;
        m_Options[OptionLegsSlot].intValue = config->ArmorSwap.m_LegsSlot;
        m_Options[OptionBootsSlot].intValue = config->ArmorSwap.m_BootsSlot;
    }

    bool ShouldRenderOption(size_t optionIndex) const override {
        if (optionIndex == OptionOpenInventory) {
            return false;
        }

        if (optionIndex == OptionPercentage) {
            return IsSwapAllEnabled();
        }

        if (optionIndex >= OptionHelmetPct && optionIndex <= OptionBootsPct) {
            return !IsSwapAllEnabled();
        }

        if (optionIndex >= OptionHelmetSlot && optionIndex <= OptionBootsSlot) {
            return IsInventoryOrganizerEnabled();
        }

        return true;
    }

    std::string GetTag() const override {
        const auto now = std::chrono::steady_clock::now();
        if (!m_StatusMsg.empty() && std::chrono::duration_cast<std::chrono::milliseconds>(now - m_StatusTime).count() < 1500) {
            return m_StatusMsg;
        }

        if (m_State == State::Opening || m_State == State::Processing || m_State == State::Organizing) {
            return "Trocando set";
        }

        if (IsMultiSwapEnabled()) {
            return "MultiSwap";
        }

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%dms", GetDelay());
        return buffer;
    }

#ifdef _BACKDOOR
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void Run(JNIEnv* env);

    static bool s_IsWorking;
#endif
};
