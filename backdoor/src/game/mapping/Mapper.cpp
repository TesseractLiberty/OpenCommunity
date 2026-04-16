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
			{"net/minecraft/client/settings/GameSettings", "avh"},
			{"net/minecraft/client/settings/KeyBinding", "avb"},
			{"net/minecraft/entity/player/EntityPlayer", "wn"},
			{"net/minecraft/inventory/Container", "xi"},
			{"net/minecraft/inventory/Slot", "yg"},
			{"net/minecraft/item/ItemStack", "zx"},
			{"net/minecraft/item/ItemArmor", "yj"},
			{"net/minecraft/item/ItemSword", "aay"},
			{"net/minecraft/item/ItemAxe", "yl"},
			{"net/minecraft/item/Item", "zw"},
			{"net/minecraft/util/MovingObjectPosition", "auh"},
			{"net/minecraft/util/MovingObjectPosition$MovingObjectType", "auh$a"},
			{"sendUseItem", "a"},
			{"attackEntity", "a"},
			{"theMinecraft", "S"},
			{"thePlayer", "h"},
			{"theWorld", "f"},
			{"currentScreen", "m"},
			{"gameSettings", "t"},
			{"leftClickCounter", "ag"},
			{"rightClickDelayTimer", "ap"},
			{"blockHitDelay", "g"},
			{"objectMouseOver", "s"},
			{"playerController", "c"},
			{"clickMouse", "aw"},
			{"rightClickMouse", "ax"},
			{"getDebugFPS", "ai"},
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
			{"getHealth", "bn"},
			{"getMaxHealth", "bu"},
			{"getHeldItem", "bA"},
			{"isSneaking", "av"},
			{"isSprinting", "aw"},
			{"setSprinting", "d"},
			{"swingItem", "bw"},
			{"isSwingInProgress", "ar"},
			{"typeOfHit", "a"},
			{"BLOCK", "b"},
			{"ENTITY", "c"},
			{"entityHit", "d"},
			{"getName", "e_"},
			{"getDisplayName", "f_"},
			{"getGameProfile", "cd"},
			{"setPosition", "b"},
			{"inventory", "bi"},
			{"inventoryContainer", "bj"},
			{"currentItem", "c"},
			{"timer", "Y"},
			{"renderPartialTicks", "c"},
			{"timerSpeed", "d"},
			{"fontRendererObj", "k"},
			{"displayWidth", "d"},
			{"displayHeight", "e"},
			{"keyBindAttack", "ai"},
			{"keyBindUseItem", "ag"},
			{"keyBindSprint", "ae"},
			{"keyBindSneak", "ad"},
			{"pressed", "h"},
			{"pressTime", "i"},
			{"setKeyBindState", "a"},
			{"onTick", "a"},
			{"keyCode", "g"},
			{"displayGuiScreen", "a"},
			{"windowClick", "a"},
			{"windowId", "d"},
			{"getSlot", "a"},
			{"getStack", "d"},
			{"item", "d"},
			{"getItem", "b"},
			{"getMaxDamage", "j"},
			{"getItemDamage", "h"},
			{"jumpTicks", "bn"},
			{"inWater", "Y"},
			{"isInWeb", "H"},
			{"width", "J"},
			{"height", "K"},
			{"boundingBox", "f"},
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
			{"net/minecraft/inventory/Container", "net/minecraft/inventory/Container"},
			{"net/minecraft/inventory/Slot", "net/minecraft/inventory/Slot"},
			{"net/minecraft/item/ItemArmor", "net/minecraft/item/ItemArmor"},
			{"theMinecraft", "theMinecraft"},
			{"thePlayer", "thePlayer"},
			{"theWorld", "theWorld"},
			{"currentScreen", "currentScreen"},
			{"gameSettings", "gameSettings"},
			{"leftClickCounter", "leftClickCounter"},
			{"rightClickDelayTimer", "rightClickDelayTimer"},
			{"blockHitDelay", "blockHitDelay"},
			{"objectMouseOver", "objectMouseOver"},
			{"playerController", "playerController"},
			{"clickMouse", "clickMouse"},
			{"rightClickMouse", "rightClickMouse"},
			{"rotationPitch", "rotationPitch"},
			{"rotationYaw", "rotationYaw"},
			{"posX", "posX"},
			{"posY", "posY"},
			{"posZ", "posZ"},
			{"motionX", "motionX"},
			{"motionY", "motionY"},
			{"motionZ", "motionZ"},
			{"onGround", "onGround"},
			{"hurtTime", "hurtTime"},
			{"hurtResistantTime", "hurtResistantTime"},
			{"getHealth", "getHealth"},
			{"getMaxHealth", "getMaxHealth"},
			{"getHeldItem", "getHeldItem"},
			{"isSneaking", "isSneaking"},
			{"isSprinting", "isSprinting"},
			{"setSprinting", "setSprinting"},
			{"swingItem", "swingItem"},
			{"typeOfHit", "typeOfHit"},
			{"BLOCK", "BLOCK"},
			{"ENTITY", "ENTITY"},
			{"entityHit", "entityHit"},
			{"getName", "getName"},
			{"getDisplayName", "getDisplayName"},
			{"setPosition", "setPosition"},
			{"inventory", "inventory"},
			{"inventoryContainer", "inventoryContainer"},
			{"currentItem", "currentItem"},
			{"timer", "timer"},
			{"renderPartialTicks", "renderPartialTicks"},
			{"timerSpeed", "timerSpeed"},
			{"fontRendererObj", "fontRendererObj"},
			{"displayWidth", "displayWidth"},
			{"displayHeight", "displayHeight"},
			{"keyBindAttack", "keyBindAttack"},
			{"keyBindSprint", "keyBindSprint"},
			{"keyBindSneak", "keyBindSneak"},
			{"pressed", "pressed"},
			{"displayGuiScreen", "displayGuiScreen"},
			{"windowClick", "windowClick"},
			{"windowId", "windowId"},
			{"getSlot", "getSlot"},
			{"getStack", "getStack"},
			{"item", "item"},
			{"getItem", "getItem"},
			{"getMaxDamage", "getMaxDamage"},
			{"getItemDamage", "getItemDamage"},
			{"jumpTicks", "jumpTicks"},
			{"inWater", "inWater"},
			{"width", "width"},
			{"height", "height"},
			{"boundingBox", "boundingBox"},
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
