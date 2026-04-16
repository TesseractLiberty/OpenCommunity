#include "pch.h"
#include "Mapper.h"

#include <unordered_map>

static std::unordered_map<std::string, std::string> g_Mappings;

void Mapper::Initialize(const GameVersions version)
{
	switch (version)
	{
	case CASUAL_1_8: {
		g_Mappings = {
			{"net/minecraft/client/Minecraft", "ave"},
			{"net/minecraft/client/entity/EntityPlayerSP", "bew"},
			{"net/minecraft/client/entity/EntityClientPlayerMP", "bew"},
			{"net/minecraft/entity/EntityLivingBase", "pr"},
			{"net/minecraft/client/multiplayer/WorldClient", "bdb"},
			{"net/minecraft/client/multiplayer/PlayerControllerMP", "bda"},
			{"net/minecraft/client/gui/GuiScreen", "axu"},
			{"net/minecraft/client/gui/inventory/GuiInventory", "azc"},
			{"net/minecraft/entity/Entity", "pk"},
			{"net/minecraft/entity/player/EntityPlayer", "wn"},
			{"net/minecraft/entity/player/InventoryPlayer", "wm"},
			{"net/minecraft/inventory/Container", "xi"},
			{"net/minecraft/inventory/Slot", "yg"},
			{"net/minecraft/item/ItemStack", "zx"},
			{"net/minecraft/item/Item", "zw"},
			{"net/minecraft/item/ItemArmor", "yj"},
			{"net/minecraft/item/ItemFood", "zs"},
			{"net/minecraft/item/ItemAppleGold", "zt"},
			{"net/minecraft/util/MovingObjectPosition", "auh"},
			{"net/minecraft/util/MovingObjectPosition$MovingObjectType", "auh$a"},
			{"net/minecraft/client/settings/GameSettings", "avh"},
			{"net/minecraft/client/settings/KeyBinding", "avb"},
			{"net/minecraft/client/renderer/entity/RenderItem", "bjh"},
			{"net/minecraft/client/renderer/RenderHelper", "avc"},
			{"net/minecraft/world/World", "adm"},
			{"net/minecraft/scoreboard/Scoreboard", "auo"},
			{"net/minecraft/scoreboard/ScoreObjective", "auk"},
			{"net/minecraft/scoreboard/Score", "aum"},
			{"net/minecraft/potion/Potion", "pe"},
			{"net/minecraft/potion/PotionEffect", "pf"},
			{"theMinecraft", "S"},
			{"thePlayer", "h"},
			{"theWorld", "f"},
			{"currentScreen", "m"},
			{"gameSettings", "t"},
			{"objectMouseOver", "s"},
			{"playerController", "c"},
			{"renderItem", "ab"},
			{"leftClickCounter", "ag"},
			{"rightClickDelayTimer", "ap"},
			{"blockHitDelay", "g"},
			{"getMaxHealth", "bu"},
			{"getScoreboard", "Z"},
			{"getObjectiveInDisplaySlot", "a"},
			{"getValueFromObjective", "c"},
			{"getScorePoints", "c"},
			{"getHeldItem", "bA"},
			{"isUsingItem", "bS"},
			{"getActivePotionEffect", "b"},
			{"jumpTicks", "bn"},
			{"rotationPitch", "z"},
			{"rotationYaw", "y"},
			{"prevRotationYaw", "A"},
			{"prevRotationPitch", "B"},
			{"posX", "s"},
			{"posY", "t"},
			{"posZ", "u"},
			{"motionX", "v"},
			{"motionY", "w"},
			{"motionZ", "x"},
			{"onGround", "C"},
			{"hurtTime", "au"},
			{"hurtResistantTime", "Z"},
			{"maxHurtResistantTime", "aD"},
			{"typeOfHit", "a"},
			{"BLOCK", "b"},
			{"ENTITY", "c"},
			{"entityHit", "d"},
			{"getName", "e_"},
			{"getGameProfile", "cd"},
			{"inventory", "bi"},
			{"inventoryContainer", "bj"},
			{"currentItem", "c"},
			{"keyBindUseItem", "ag"},
			{"setKeyBindState", "a"},
			{"onTick", "a"},
			{"keyCode", "g"},
			{"windowClick", "a"},
			{"windowId", "d"},
			{"getSlot", "a"},
			{"getStack", "d"},
			{"item", "d"},
			{"metadata", "f"},
			{"getItem", "b"},
			{"getCurrentArmor", "q"},
			{"getStackInSlot", "a"},
			{"getMaxDamage", "j"},
			{"getItemDamage", "h"},
			{"sendUseItem", "a"},
			{"updateController", "e"},
			{"renderItemIntoGUI", "a"},
			{"enableGUIStandardItemLighting", "c"},
			{"disableStandardItemLighting", "a"},
			{"regeneration", "l"},
			{"potionTypes", "a"},
			{"duration", "c"},
			{"getDuration", "b"},
			{"width", "J"},
			{"height", "K"}
		};
		break;
	}
	case LUNAR_1_8: {
		g_Mappings = {
			{"net/minecraft/client/Minecraft", "net/minecraft/client/Minecraft"},
			{"net/minecraft/client/entity/EntityPlayerSP", "net/minecraft/client/entity/EntityPlayerSP"},
			{"net/minecraft/client/entity/EntityClientPlayerMP", "net/minecraft/client/entity/EntityPlayerSP"},
			{"net/minecraft/entity/EntityLivingBase", "net/minecraft/entity/EntityLivingBase"},
			{"net/minecraft/client/multiplayer/WorldClient", "net/minecraft/client/multiplayer/WorldClient"},
			{"net/minecraft/client/multiplayer/PlayerControllerMP", "net/minecraft/client/multiplayer/PlayerControllerMP"},
			{"net/minecraft/client/gui/GuiScreen", "net/minecraft/client/gui/GuiScreen"},
			{"net/minecraft/client/gui/inventory/GuiInventory", "net/minecraft/client/gui/inventory/GuiInventory"},
			{"net/minecraft/entity/Entity", "net/minecraft/entity/Entity"},
			{"net/minecraft/entity/player/EntityPlayer", "net/minecraft/entity/player/EntityPlayer"},
			{"net/minecraft/entity/player/InventoryPlayer", "net/minecraft/entity/player/InventoryPlayer"},
			{"net/minecraft/inventory/Container", "net/minecraft/inventory/Container"},
			{"net/minecraft/inventory/Slot", "net/minecraft/inventory/Slot"},
			{"net/minecraft/item/ItemStack", "net/minecraft/item/ItemStack"},
			{"net/minecraft/item/Item", "net/minecraft/item/Item"},
			{"net/minecraft/item/ItemArmor", "net/minecraft/item/ItemArmor"},
			{"net/minecraft/item/ItemFood", "net/minecraft/item/ItemFood"},
			{"net/minecraft/item/ItemAppleGold", "net/minecraft/item/ItemAppleGold"},
			{"net/minecraft/util/MovingObjectPosition", "net/minecraft/util/MovingObjectPosition"},
			{"net/minecraft/util/MovingObjectPosition$MovingObjectType", "net/minecraft/util/MovingObjectPosition$MovingObjectType"},
			{"net/minecraft/client/settings/GameSettings", "net/minecraft/client/settings/GameSettings"},
			{"net/minecraft/client/settings/KeyBinding", "net/minecraft/client/settings/KeyBinding"},
			{"net/minecraft/client/renderer/entity/RenderItem", "net/minecraft/client/renderer/entity/RenderItem"},
			{"net/minecraft/client/renderer/RenderHelper", "net/minecraft/client/renderer/RenderHelper"},
			{"net/minecraft/world/World", "net/minecraft/world/World"},
			{"net/minecraft/scoreboard/Scoreboard", "net/minecraft/scoreboard/Scoreboard"},
			{"net/minecraft/scoreboard/ScoreObjective", "net/minecraft/scoreboard/ScoreObjective"},
			{"net/minecraft/scoreboard/Score", "net/minecraft/scoreboard/Score"},
			{"net/minecraft/potion/Potion", "net/minecraft/potion/Potion"},
			{"net/minecraft/potion/PotionEffect", "net/minecraft/potion/PotionEffect"},
			{"theMinecraft", "theMinecraft"},
			{"thePlayer", "thePlayer"},
			{"theWorld", "theWorld"},
			{"currentScreen", "currentScreen"},
			{"gameSettings", "gameSettings"},
			{"objectMouseOver", "objectMouseOver"},
			{"playerController", "playerController"},
			{"renderItem", "renderItem"},
			{"leftClickCounter", "leftClickCounter"},
			{"rightClickDelayTimer", "rightClickDelayTimer"},
			{"blockHitDelay", "blockHitDelay"},
			{"getMaxHealth", "getMaxHealth"},
			{"getScoreboard", "getScoreboard"},
			{"getObjectiveInDisplaySlot", "getObjectiveInDisplaySlot"},
			{"getValueFromObjective", "getValueFromObjective"},
			{"getScorePoints", "getScorePoints"},
			{"getHeldItem", "getHeldItem"},
			{"isUsingItem", "isUsingItem"},
			{"getActivePotionEffect", "getActivePotionEffect"},
			{"jumpTicks", "jumpTicks"},
			{"rotationPitch", "rotationPitch"},
			{"rotationYaw", "rotationYaw"},
			{"prevRotationYaw", "prevRotationYaw"},
			{"prevRotationPitch", "prevRotationPitch"},
			{"posX", "posX"},
			{"posY", "posY"},
			{"posZ", "posZ"},
			{"motionX", "motionX"},
			{"motionY", "motionY"},
			{"motionZ", "motionZ"},
			{"onGround", "onGround"},
			{"hurtTime", "hurtTime"},
			{"hurtResistantTime", "hurtResistantTime"},
			{"maxHurtResistantTime", "maxHurtResistantTime"},
			{"typeOfHit", "typeOfHit"},
			{"BLOCK", "BLOCK"},
			{"ENTITY", "ENTITY"},
			{"entityHit", "entityHit"},
			{"getName", "getName"},
			{"getGameProfile", "getGameProfile"},
			{"inventory", "inventory"},
			{"inventoryContainer", "inventoryContainer"},
			{"currentItem", "currentItem"},
			{"keyBindUseItem", "keyBindUseItem"},
			{"setKeyBindState", "setKeyBindState"},
			{"onTick", "onTick"},
			{"keyCode", "keyCode"},
			{"windowClick", "windowClick"},
			{"windowId", "windowId"},
			{"getSlot", "getSlot"},
			{"getStack", "getStack"},
			{"item", "item"},
			{"metadata", "itemDamage"},
			{"getItem", "getItem"},
			{"getCurrentArmor", "getCurrentArmor"},
			{"getStackInSlot", "getStackInSlot"},
			{"getMaxDamage", "getMaxDamage"},
			{"getItemDamage", "getItemDamage"},
			{"sendUseItem", "sendUseItem"},
			{"updateController", "updateController"},
			{"renderItemIntoGUI", "renderItemIntoGUI"},
			{"enableGUIStandardItemLighting", "enableGUIStandardItemLighting"},
			{"disableStandardItemLighting", "disableStandardItemLighting"},
			{"regeneration", "regeneration"},
			{"potionTypes", "potionTypes"},
			{"duration", "duration"},
			{"getDuration", "getDuration"},
			{"setJumping", "setJumping"},
			{"width", "width"},
			{"height", "height"}
		};
		break;
	}
	default:
		break;
	}
}

std::string Mapper::Get(const char* mapping, int type)
{
	auto it = g_Mappings.find(mapping);
	if (it == g_Mappings.end())
		return std::string("");

	std::string ret;
	switch (type)
	{
	case 1:
		ret = it->second;
		break;
	case 2:
		ret = "L" + it->second + ";";
		break;
	case 3:
		ret = "()L" + it->second + ";";
		break;
	default:
		ret = it->second;
		break;
	}

	return ret;
}
