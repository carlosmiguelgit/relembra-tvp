/**
 * The Violet Project - a free and open-source MMORPG server emulator
 * Copyright (C) 2021 - Ezzz <alejandromujica.rsm@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "combat.h"
#include "configmanager.h"
#include "game.h"
#include "monster.h"
#include "pugicast.h"
#include "spells.h"

extern Game g_game;
extern Spells* g_spells;
extern Monsters g_monsters;
extern Vocations g_vocations;
extern ConfigManager g_config;
extern LuaEnvironment g_luaEnvironment;

Spells::Spells()
{
	scriptInterface.initState();
}

Spells::~Spells()
{
	clear(false);
}

TalkActionResult_t Spells::playerSaySpell(Player* player, std::string& words)
{
	std::string str_words = words;

	//strip trailing spaces
	trimString(str_words);
	str_words = removeExtraSpaces(str_words);

	InstantSpell* instantSpell = getInstantSpell(str_words);
	if (!instantSpell) {
		return TALKACTION_CONTINUE;
	}

	const std::string& spellWords = instantSpell->getWords();
	int32_t instantLen = mergeSpellWords(spellWords).length() + countSpaces(spellWords);
	int32_t givenLen = instantLen + 1 - countSpaces(spellWords);

	std::string param;
	if (givenLen < str_words.length()) {
		param = str_words.substr(givenLen, str_words.length());
	}

	bool hasMultipleApostrophe = false;
	bool hasApostrophe = false;

	if (!param.empty()) {
		size_t paramStart = param.find_first_of('"');
		if (paramStart != std::string::npos) {
			hasApostrophe = true;
			if (param[paramStart + 1] == '"') {
				hasMultipleApostrophe = true;
			}
		}

		replaceString(param, "\"", "");

		if (param[0] == ' ') {
			param = param.substr(1, param.length());
		}
	}

	if (instantSpell->getHasParam()) {
		// only single word params without apostrophes
		if (!hasApostrophe && param.find_first_of(' ') != std::string::npos) {
			return TALKACTION_CONTINUE;
		}

		if (param.empty()) {
			return TALKACTION_CONTINUE;
		}
	} else if (!param.empty() && !hasMultipleApostrophe) {
		return TALKACTION_CONTINUE;
	}

	if (instantSpell->playerCastInstant(player, param)) {
		return TALKACTION_BREAK;
	}

	return TALKACTION_FAILED;
}

void Spells::clearMaps(bool fromLua)
{
	for (auto instant = instants.begin(); instant != instants.end(); ) {
		if (fromLua == instant->second.fromLua) {
			instant = instants.erase(instant);
		} else {
			++instant;
		}
	}

	for (auto rune = runes.begin(); rune != runes.end(); ) {
		if (fromLua == rune->second.fromLua) {
			rune = runes.erase(rune);
		} else {
			++rune;
		}
	}
}

void Spells::clear(bool fromLua)
{
	clearMaps(fromLua);

	reInitState(fromLua);
}

LuaScriptInterface& Spells::getScriptInterface()
{
	return scriptInterface;
}

std::string Spells::getScriptBaseName() const
{
	return "spells";
}

Event_ptr Spells::getEvent(const std::string& nodeName)
{
	if (strcasecmp(nodeName.c_str(), "rune") == 0) {
		return Event_ptr(new RuneSpell(&scriptInterface));
	} else if (strcasecmp(nodeName.c_str(), "instant") == 0) {
		return Event_ptr(new InstantSpell(&scriptInterface));
	}
	return nullptr;
}

bool Spells::registerEvent(Event_ptr event, const pugi::xml_node&)
{
	InstantSpell* instant = dynamic_cast<InstantSpell*>(event.get());
	if (instant) {
		auto result = instants.emplace(instant->getWords(), std::move(*instant));
		if (!result.second) {
			std::cout << "[Warning - Spells::registerEvent] Duplicate registered instant spell with words: " << instant->getWords() << std::endl;
		}
		return result.second;
	}

	RuneSpell* rune = dynamic_cast<RuneSpell*>(event.get());
	if (rune) {
		auto result = runes.emplace(rune->getRuneItemId(), std::move(*rune));
		if (!result.second) {
			std::cout << "[Warning - Spells::registerEvent] Duplicate registered rune with id: " << rune->getRuneItemId() << std::endl;
		}
		return result.second;
	}

	return false;
}

bool Spells::registerInstantLuaEvent(InstantSpell* event)
{
	InstantSpell_ptr instant { event };
	if (instant) {
		std::string words = instant->getWords();
		auto result = instants.emplace(instant->getWords(), std::move(*instant));
		if (!result.second) {
			std::cout << "[Warning - Spells::registerInstantLuaEvent] Duplicate registered instant spell with words: " << words << std::endl;
		}
		return result.second;
	}

	return false;
}

bool Spells::registerRuneLuaEvent(RuneSpell* event)
{
	RuneSpell_ptr rune { event };
	if (rune) {
		uint16_t id = rune->getRuneItemId();
		auto result = runes.emplace(rune->getRuneItemId(), std::move(*rune));
		if (!result.second) {
			std::cout << "[Warning - Spells::registerRuneLuaEvent] Duplicate registered rune with id: " << id << std::endl;
		}
		return result.second;
	}

	return false;
}

Spell* Spells::getSpellByName(const std::string& name)
{
	Spell* spell = getRuneSpellByName(name);
	if (!spell) {
		spell = getInstantSpellByName(name);
	}
	return spell;
}

RuneSpell* Spells::getRuneSpell(uint32_t id)
{
	auto it = runes.find(id);
	if (it == runes.end()) {
		for (auto& rune : runes) {
			if (rune.second.getId() == id) {
				return &rune.second;
			}
		}
		return nullptr;
	}
	return &it->second;
}

RuneSpell* Spells::getRuneSpellByName(const std::string& name)
{
	for (auto& it : runes) {
		if (strcasecmp(it.second.getName().c_str(), name.c_str()) == 0) {
			return &it.second;
		}
	}
	return nullptr;
}

InstantSpell* Spells::getInstantSpell(const std::string& words)
{
	InstantSpell* result = nullptr;

	std::string constructedWords = words;

	size_t paramStart = words.find_first_of('"');
	if (paramStart != std::string::npos) { // String has a "
		size_t safeParamStart = std::min<size_t>(words.length() - 1, paramStart - 1);
		if (words[safeParamStart] != ' ') { // Do not allow to cast spells as: exura", only exura "
			return nullptr;
		}

		constructedWords = words.substr(0, paramStart - 1);
	}

	for (auto& it : instants) {
		const std::string& instantSpellWords = it.second.getWords();
		
		// match the current spell words with the given words
		if (compareSpellWords(instantSpellWords, constructedWords, (it.second.getHasParam() || it.second.getHasPlayerNameParam()))) {
			result = &it.second;
		}

		// continue checking on spells
	}

	return result;
}

InstantSpell* Spells::getInstantSpellByName(const std::string& name)
{
	for (auto& it : instants) {
		if (strcasecmp(it.second.getName().c_str(), name.c_str()) == 0) {
			return &it.second;
		}
	}
	return nullptr;
}

Position Spells::getCasterPosition(Creature* creature, Direction dir)
{
	return getNextPosition(dir, creature->getPosition());
}

CombatSpell::CombatSpell(Combat_ptr combat, bool needTarget, bool needDirection) :
	Event(&g_spells->getScriptInterface()),
	combat(combat),
	needDirection(needDirection),
	needTarget(needTarget)
{}

bool CombatSpell::loadScriptCombat()
{
	combat = g_luaEnvironment.getCombatObject(g_luaEnvironment.lastCombatId);
	return combat != nullptr;
}

bool CombatSpell::castSpell(Creature* creature)
{
	if (scripted) {
		LuaVariant var;
		var.type = VARIANT_POSITION;

		if (needDirection) {
			var.pos = Spells::getCasterPosition(creature, creature->getDirection());
		} else {
			var.pos = creature->getPosition();
		}

		return executeCastSpell(creature, var);
	}

	Position pos;
	if (needDirection) {
		pos = Spells::getCasterPosition(creature, creature->getDirection());
	} else {
		pos = creature->getPosition();
	}

	combat->doCombat(creature, pos);
	return true;
}

bool CombatSpell::castSpell(Creature* creature, Creature* target)
{
	if (scripted) {
		LuaVariant var;

		if (combat->hasArea()) {
			var.type = VARIANT_POSITION;

			if (needTarget) {
				var.pos = target->getPosition();
			} else if (needDirection) {
				var.pos = Spells::getCasterPosition(creature, creature->getDirection());
			} else {
				var.pos = creature->getPosition();
			}
		} else {
			var.type = VARIANT_NUMBER;
			var.number = target->getID();
		}
		return executeCastSpell(creature, var);
	}

	if (combat->hasArea()) {
		if (needTarget) {
			combat->doCombat(creature, target->getPosition());
		} else {
			return castSpell(creature);
		}
	} else {
		combat->doCombat(creature, target);
	}
	return true;
}

bool CombatSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	//onCastSpell(creature, var)
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - CombatSpell::executeCastSpell] Call stack overflow" << std::endl;
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	LuaScriptInterface::pushUserdata<Creature>(L, creature);
	LuaScriptInterface::setCreatureMetatable(L, -1, creature);

	LuaScriptInterface::pushVariant(L, var);

	return scriptInterface->callFunction(2);
}

bool Spell::configureSpell(const pugi::xml_node& node)
{
	pugi::xml_attribute nameAttribute = node.attribute("name");
	if (!nameAttribute) {
		std::cout << "[Error - Spell::configureSpell] Spell without name" << std::endl;
		return false;
	}

	name = nameAttribute.as_string();

	static const char* reservedList[] = {
		"melee",
		"physical",
		"poison",
		"fire",
		"energy",
		"drown",
		"lifedrain",
		"manadrain",
		"healing",
		"speed",
		"outfit",
		"invisible",
		"drunk",
		"firefield",
		"poisonfield",
		"energyfield",
		"firecondition",
		"poisoncondition",
		"energycondition",
	};

	//static size_t size = sizeof(reservedList) / sizeof(const char*);
	//for (size_t i = 0; i < size; ++i) {
	for (const char* reserved : reservedList) {
		if (strcasecmp(reserved, name.c_str()) == 0) {
			std::cout << "[Error - Spell::configureSpell] Spell is using a reserved name: " << reserved << std::endl;
			return false;
		}
	}

	pugi::xml_attribute attr;
	if ((attr = node.attribute("spellid"))) {
		spellId = pugi::cast<uint16_t>(attr.value());
	}

	if ((attr = node.attribute("level")) || (attr = node.attribute("lvl"))) {
		level = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = node.attribute("magiclevel")) || (attr = node.attribute("maglv"))) {
		magLevel = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = node.attribute("mana"))) {
		mana = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = node.attribute("manapercent"))) {
		manaPercent = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = node.attribute("soul"))) {
		soul = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = node.attribute("range"))) {
		range = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("premium")) || (attr = node.attribute("prem"))) {
		premium = attr.as_bool();
	}

	if ((attr = node.attribute("enabled"))) {
		enabled = attr.as_bool();
	}

	if ((attr = node.attribute("needtarget"))) {
		needTarget = attr.as_bool();
	}

	if ((attr = node.attribute("needweapon"))) {
		needWeapon = attr.as_bool();
	}

	if ((attr = node.attribute("selftarget"))) {
		selfTarget = attr.as_bool();
	}

	if ((attr = node.attribute("needlearn"))) {
		learnable = attr.as_bool();
	}

	if ((attr = node.attribute("cooldownSpellTime"))) {
		cooldownSpellTime = attr.as_bool();
	}

	if ((attr = node.attribute("category"))) {
		spellCategory = static_cast<SpellCategory_t>(attr.as_uint());
	}

	if ((attr = node.attribute("blocking"))) {
		blockingSolid = attr.as_bool();
		blockingCreature = blockingSolid;
	}

	if ((attr = node.attribute("blocktype"))) {
		std::string tmpStrValue = asLowerCaseString(attr.as_string());
		if (tmpStrValue == "all") {
			blockingSolid = true;
			blockingCreature = true;
		} else if (tmpStrValue == "solid") {
			blockingSolid = true;
		} else if (tmpStrValue == "creature") {
			blockingCreature = true;
		} else {
			std::cout << "[Warning - Spell::configureSpell] Blocktype \"" << attr.as_string() << "\" does not exist." << std::endl;
		}
	}

	if ((attr = node.attribute("pzlock"))) {
		pzLock = booleanString(attr.as_string());
	}

	if ((attr = node.attribute("aggressive"))) {
		aggressive = booleanString(attr.as_string());
	}

	for (auto vocationNode : node.children()) {
		if (!(attr = vocationNode.attribute("name"))) {
			continue;
		}

		int32_t vocationId = g_vocations.getVocationId(attr.as_string());
		if (vocationId != -1) {
			attr = vocationNode.attribute("showInDescription");
			vocSpellMap[vocationId] = !attr || attr.as_bool();
		} else {
			std::cout << "[Warning - Spell::configureSpell] Wrong vocation name: " << attr.as_string() << std::endl;
		}
	}
	return true;
}

bool Spell::playerSpellCheck(Player* player) const
{
	if (player->hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

 if (player->hasFlag(PlayerFlag_IgnoreSpellCheck) || g_config.getBoolean(ConfigManager::NO_SPELL_REQUIREMENTS)) {
		return true;
	}

	if (!enabled) {
		return false;
	}

	if (isInstant() && isLearnable() && g_config.getBoolean(ConfigManager::NEED_LEARN_SPELLS)) {
		if (!player->hasLearnedInstantSpell(getName())) {
			player->sendCancelMessage(RETURNVALUE_YOUNEEDTOLEARNTHISSPELL);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
			return false;
		}
	} 
	if (!vocSpellMap.empty() && vocSpellMap.find(player->getVocationId()) == vocSpellMap.end()) {
		player->sendCancelMessage(RETURNVALUE_YOURVOCATIONCANNOTUSETHISSPELL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (player->getMagicLevel() < magLevel) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHMAGICLEVEL);
		// there is no poff for not enouh level
		// g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (player->getLevel() < level) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHLEVEL);
		// there is no poff for not enouh level
		// g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if ((aggressive || pzLock) && player->hasCondition(CONDITION_PACIFIED)) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (OTSYS_TIME() < player->earliestSpellTime) {
		player->sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		if (isInstant()) {
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		}
		return false;
	}

	if ((aggressive || pzLock) && !player->hasFlag(PlayerFlag_IgnoreProtectionZone) && player->getZone() == ZONE_PROTECTION) {
		player->sendCancelMessage(RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}


	if (player->getSoul() < soul && !player->hasFlag(PlayerFlag_HasInfiniteSoul)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHSOUL);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}
	
	if (player->getMana() < getManaCost(player) && !player->hasFlag(PlayerFlag_HasInfiniteMana)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHMANA);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}


	if (needWeapon) {
		switch (player->getWeaponType()) {
			case WEAPON_SWORD:
			case WEAPON_CLUB:
			case WEAPON_AXE:
				break;

			default: {
				player->sendCancelMessage(RETURNVALUE_YOUNEEDAWEAPONTOUSETHISSPELL);
				g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
				return false;
			}
		}
	}

	if (isPremium() && !player->isPremium()) {
		player->sendCancelMessage(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	return true;
}

bool Spell::playerInstantSpellCheck(Player* player, const Position& toPos)
{
	if (toPos.x == 0xFFFF) {
		return true;
	}

	const Position& playerPos = player->getPosition();
	if (playerPos.z != toPos.z) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile) {
		tile = new StaticTile(toPos.x, toPos.y, toPos.z);
		g_game.map.setTile(toPos, tile);
	}

	if (blockingCreature && tile->getBottomCreature() != nullptr) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (blockingSolid && tile->hasFlag(TILESTATE_BLOCKSOLID)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	return true;
}

bool Spell::playerRuneSpellCheck(Player* player, const Position& toPos)
{
	if (!playerSpellCheck(player)) {
		return false;
	}

	if (toPos.x == 0xFFFF) {
		return true;
	}

	const Position& playerPos = player->getPosition();
	if (playerPos.z > toPos.z) {
		player->sendCancelMessage(RETURNVALUE_FIRSTGOUPSTAIRS);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	} else if (playerPos.z < toPos.z) {
		player->sendCancelMessage(RETURNVALUE_FIRSTGODOWNSTAIRS);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile) {
		player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (!g_game.canThrowObjectTo(playerPos, toPos, false)) {
		player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (range != -1) {
		if ((Position::getDistanceX(playerPos, toPos) > range) ||
			(Position::getDistanceY(playerPos, toPos) > range)) {
			player->sendCancelMessage(RETURNVALUE_DESTINATIONOUTOFREACH);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
			return false;
		}
	}

	ReturnValue ret = Combat::canDoCombat(player, tile, aggressive);
	if (ret != RETURNVALUE_NOERROR) {
		player->sendCancelMessage(ret);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	const Creature* bottomVisibleCreature = tile->getBottomCreature();
	if (blockingCreature && bottomVisibleCreature) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	} else if (blockingSolid && tile->hasProperty(CONST_PROP_BLOCKPROJECTILE)) {
		player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	if (needTarget) {
		if (!bottomVisibleCreature) {
			player->sendCancelMessage(RETURNVALUE_CANONLYUSETHISRUNEONCREATURES);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
			return false;
		}
	}

	if (aggressive && needTarget && bottomVisibleCreature && player->hasSecureMode()) {
		const Player* targetPlayer = bottomVisibleCreature->getPlayer();
		if (targetPlayer && targetPlayer != player && player->getSkullClient(targetPlayer) == SKULL_NONE && !Combat::isInPvpZone(player, targetPlayer)) {
			player->sendCancelMessage(RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS);
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
			return false;
		}
	}
	return true;
}

void Spell::postCastSpell(Player* player, bool finishedCast /*= true*/, bool payCost /*= true*/) const
{
	if (finishedCast) {
		if (!player->hasFlag(PlayerFlag_HasNoExhaustion) && cooldownSpellTime) {
			int32_t delay = getCooldown();
			if (!aggressive) {
				delay = 1000;
			}

			if (player->earliestSpellTime < OTSYS_TIME() + delay) {
				player->earliestSpellTime = OTSYS_TIME() + delay;
			}
		}

		if (aggressive) {
			player->addInFightTicks();
		}
	}

	if (payCost) {
		Spell::postCastSpell(player, getManaCost(player), getSoulCost());
	}
}

void Spell::postCastSpell(Player* player, uint32_t manaCost, uint32_t soulCost)
{
	if (g_config.getBoolean(ConfigManager::UNLIMITED_PLAYER_MP)) {
		return;
	}

	if (manaCost > 0) {
		player->addManaSpent(manaCost);
		player->changeMana(-static_cast<int32_t>(manaCost));
	}

	if (!player->hasFlag(PlayerFlag_HasInfiniteSoul)) {
		if (soulCost > 0) {
			player->changeSoul(-static_cast<int32_t>(soulCost));
		}
	}
}

uint32_t Spell::getManaCost(const Player* player) const
{
	if (mana != 0) {
		return mana;
	}

	if (manaPercent != 0) {
		uint32_t maxMana = player->getMaxMana();
		uint32_t manaCost = (maxMana * manaPercent) / 100;
		return manaCost;
	}

	return 0;
}

std::string InstantSpell::getScriptEventName() const
{
	return "onCastSpell";
}

bool InstantSpell::configureEvent(const pugi::xml_node& node)
{
	if (!Spell::configureSpell(node)) {
		return false;
	}

	if (!TalkAction::configureEvent(node)) {
		return false;
	}

	spellType = SPELL_INSTANT;

	pugi::xml_attribute attr;
	if ((attr = node.attribute("params"))) {
		hasParam = attr.as_bool();
	}

	if ((attr = node.attribute("playernameparam"))) {
		hasPlayerNameParam = attr.as_bool();
	}

	if ((attr = node.attribute("direction"))) {
		needDirection = attr.as_bool();
	} else if ((attr = node.attribute("casterTargetOrDirection"))) {
		casterTargetOrDirection = attr.as_bool();
	}

	if ((attr = node.attribute("blockwalls"))) {
		checkLineOfSight = attr.as_bool();
	}

	return true;
}

bool InstantSpell::playerCastInstant(Player* player, std::string& param)
{
	if (!playerSpellCheck(player)) {
		return false;
	}

	LuaVariant var;

	if (selfTarget) {
		var.type = VARIANT_NUMBER;
		var.number = player->getID();
	} else if (needTarget || casterTargetOrDirection) {
		Creature* target = nullptr;
		bool useDirection = false;

		if (hasParam) {
			Player* playerTarget = nullptr;
			ReturnValue ret = g_game.getPlayerByNameWildcard(param, playerTarget);

			if (playerTarget && playerTarget->isAccessPlayer() && !player->isAccessPlayer()) {
				playerTarget = nullptr;
			}

			target = playerTarget;
			if (!target || target->getHealth() <= 0) {
				if (!casterTargetOrDirection) {
					player->sendCancelMessage(ret);
					g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
					return false;
				}

				useDirection = true;
			}

			if (playerTarget) {
				param = playerTarget->getName();
			}
		} else {
			target = player->getAttackedCreature();
			if (!target || target->getHealth() <= 0) {
				if (!casterTargetOrDirection) {
					player->sendCancelMessage(RETURNVALUE_YOUCANONLYUSEITONCREATURES);
					g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
					return false;
				}

				useDirection = true;
			}
		}

		if (!useDirection) {
			if (!canThrowSpell(player, target)) {
				player->sendCancelMessage(RETURNVALUE_CREATUREISNOTREACHABLE);
				g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
				return false;
			}

			var.type = VARIANT_NUMBER;
			var.number = target->getID();
		} else {
			var.type = VARIANT_POSITION;
			var.pos = Spells::getCasterPosition(player, player->getDirection());

			if (!playerInstantSpellCheck(player, var.pos)) {
				return false;
			}
		}
	} else if (hasParam) {
		var.type = VARIANT_STRING;

		if (getHasPlayerNameParam()) {
			Player* playerTarget = nullptr;
			ReturnValue ret = g_game.getPlayerByNameWildcard(param, playerTarget);

			if (ret != RETURNVALUE_NOERROR) {
				player->sendCancelMessage(ret);
				g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
				return false;
			}

			if (playerTarget && (!playerTarget->isAccessPlayer() || player->isAccessPlayer())) {
				param = playerTarget->getName();
			}
		}

		var.text = param;
	} else {
		var.type = VARIANT_POSITION;

		if (needDirection) {
			var.pos = Spells::getCasterPosition(player, player->getDirection());
		} else {
			var.pos = player->getPosition();
		}

		if (!playerInstantSpellCheck(player, var.pos)) {
			return false;
		}
	}

	bool result = internalCastSpell(player, var);
	if (result) {
		postCastSpell(player);
	}

	return result;
}

bool InstantSpell::canThrowSpell(const Creature* creature, const Creature* target) const
{
	const Position& fromPos = creature->getPosition();
	const Position& toPos = target->getPosition();

	if (fromPos.z != toPos.z) {
		return false;
	}

	if (checkLineOfSight && !g_game.canThrowObjectTo(fromPos, toPos, false)) {
		return false;
	}

	if (range != -1) {
		if ((Position::getDistanceX(fromPos, toPos) > range) ||
			(Position::getDistanceY(fromPos, toPos) > range)) {
			return false;
		}
	}

	return true;
}

bool InstantSpell::castSpell(Creature* creature)
{
	LuaVariant var;

	if (casterTargetOrDirection) {
		Creature* target = creature->getAttackedCreature();
		if (target && target->getHealth() > 0) {
			if (!canThrowSpell(creature, target)) {
				return false;
			}

			var.type = VARIANT_NUMBER;
			var.number = target->getID();
			return internalCastSpell(creature, var);
		}

		return false;
	} else if (needDirection) {
		var.type = VARIANT_POSITION;
		var.pos = Spells::getCasterPosition(creature, creature->getDirection());
	} else {
		var.type = VARIANT_POSITION;
		var.pos = creature->getPosition();
	}

	return internalCastSpell(creature, var);
}

bool InstantSpell::castSpell(Creature* creature, Creature* target)
{
	if (needTarget) {
		LuaVariant var;
		var.type = VARIANT_NUMBER;
		var.number = target->getID();
		return internalCastSpell(creature, var);
	} else {
		return castSpell(creature);
	}
}

bool InstantSpell::internalCastSpell(Creature* creature, const LuaVariant& var)
{
	return executeCastSpell(creature, var);
}

bool InstantSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	//onCastSpell(creature, var)
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - InstantSpell::executeCastSpell] Call stack overflow" << std::endl;
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	LuaScriptInterface::pushUserdata<Creature>(L, creature);
	LuaScriptInterface::setCreatureMetatable(L, -1, creature);

	LuaScriptInterface::pushVariant(L, var);

	return scriptInterface->callFunction(2);
}

bool InstantSpell::canCast(const Player* player) const
{
	if (player->hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

	if (player->hasFlag(PlayerFlag_IgnoreSpellCheck) ||
		g_config.getBoolean(ConfigManager::NO_SPELL_REQUIREMENTS)) {
		return true;
	}

	if (isLearnable() && g_config.getBoolean(ConfigManager::NEED_LEARN_SPELLS)) {
		if (player->hasLearnedInstantSpell(getName())) {
			return true;
		}
	} else {
		if (vocSpellMap.empty() || vocSpellMap.find(player->getVocationId()) != vocSpellMap.end()) {
			return true;
		}
	}

	return false;
}

std::string RuneSpell::getScriptEventName() const
{
	return "onCastSpell";
}

bool RuneSpell::configureEvent(const pugi::xml_node& node)
{
	if (!Spell::configureSpell(node)) {
		return false;
	}

	if (!Action::configureEvent(node)) {
		return false;
	}

	spellType = SPELL_RUNE;

	pugi::xml_attribute attr;
	if (!(attr = node.attribute("id"))) {
		std::cout << "[Error - RuneSpell::configureSpell] Rune spell without id." << std::endl;
		return false;
	}
	runeId = pugi::cast<uint16_t>(attr.value());

	if ((attr = node.attribute("charges"))) {
		charges = pugi::cast<uint32_t>(attr.value());
	} else {
		charges = 0;
	}

	hasCharges = (charges > 0);
	if (magLevel != 0 || level != 0) {
		//Change information in the ItemType to get accurate description
		ItemType& iType = Item::items.getItemType(runeId);
		iType.runeMagLevel = magLevel;
		iType.runeLevel = level;
		iType.charges = charges;
	}

	return true;
}

ReturnValue RuneSpell::canExecuteAction(const Player* player, const Position& toPos)
{
	if (player->hasFlag(PlayerFlag_CannotUseSpells)) {
		return RETURNVALUE_CANNOTUSETHISOBJECT;
	}

	ReturnValue ret = Action::canExecuteAction(player, toPos);
	if (ret != RETURNVALUE_NOERROR) {
		return ret;
	}

	if (toPos.x == 0xFFFF) {
		if (needTarget) {
			return RETURNVALUE_CANONLYUSETHISRUNEONCREATURES;
		} else if (!selfTarget) {
			return RETURNVALUE_NOTENOUGHROOM;
		}
	}

	return RETURNVALUE_NOERROR;
}

bool RuneSpell::executeUse(Player* player, Item* item, const Position&, Thing* target, const Position& toPosition)
{
	if (!playerRuneSpellCheck(player, toPosition)) {
		return false;
	}

	if (!scripted) {
		return false;
	}

	LuaVariant var;

	if (needTarget) {
		var.type = VARIANT_NUMBER;

		if (target == nullptr) {
			Tile* toTile = g_game.map.getTile(toPosition);
			if (toTile) {
				const Creature* visibleCreature = toTile->getBottomCreature();
				if (visibleCreature) {
					var.number = visibleCreature->getID();
				}
			}
		} else {
			var.number = target->getCreature()->getID();
		}
	} else {
		var.type = VARIANT_POSITION;
		var.pos = toPosition;
	}

	if (!internalCastSpell(player, var)) {
		g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF);
		return false;
	}

	postCastSpell(player);

	target = g_game.getCreatureByID(var.number);
	if (getPzLock()) {
		player->addInFightTicks(true);
	}

	if (hasCharges && item && g_config.getBoolean(ConfigManager::REMOVE_RUNE_CHARGES)) {
		int32_t newCount = std::max<int32_t>(0, item->getCharges() - 1);
		if (newCount <= 0) {
			g_game.internalRemoveItem(item);
		} else {
			g_game.transformItem(item, item->getID(), newCount);
		}
	}

	return true;
}

bool RuneSpell::castSpell(Creature* creature)
{
	LuaVariant var;
	var.type = VARIANT_NUMBER;
	var.number = creature->getID();
	return internalCastSpell(creature, var);
}

bool RuneSpell::castSpell(Creature* creature, Creature* target)
{
	LuaVariant var;
	var.type = VARIANT_NUMBER;
	var.number = target->getID();
	return internalCastSpell(creature, var);
}

bool RuneSpell::internalCastSpell(Creature* creature, const LuaVariant& var)
{
	bool result;
	if (scripted) {
		result = executeCastSpell(creature, var);
	} else {
		result = false;
	}
	return result;
}

bool RuneSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	//onCastSpell(creature, var)
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - RuneSpell::executeCastSpell] Call stack overflow" << std::endl;
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(scriptId);

	LuaScriptInterface::pushUserdata<Creature>(L, creature);
	LuaScriptInterface::setCreatureMetatable(L, -1, creature);

	LuaScriptInterface::pushVariant(L, var);

	return scriptInterface->callFunction(2);
}
