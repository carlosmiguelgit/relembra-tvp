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

#include "raids.h"

#include "pugicast.h"

#include "game.h"
#include "configmanager.h"
#include "scheduler.h"
#include "monster.h"
#include "database.h"
#include "databasetasks.h"

#include <fmt/format.h>

extern Game g_game;
extern ConfigManager g_config;

Raids::Raids()
{
	scriptInterface.initState();
}

Raids::~Raids()
{
	for (Raid* raid : raidList) {
		delete raid;
	}
}

bool Raids::loadFromXml()
{
	if (isLoaded()) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/raids/raids.xml");
	if (!result) {
		printXMLError("Error - Raids::loadFromXml", "data/raids/raids.xml", result);
		return false;
	}

	for (auto raidNode : doc.child("raids").children()) {
		std::string name, file;
		uint32_t interval = 0;
		time_t date = 0;
		bool log = false;

		pugi::xml_attribute attr;
		if ((attr = raidNode.attribute("name"))) {
			name = attr.as_string();
		} else {
			std::cout << "[Error - Raids::loadFromXml] Name tag missing for raid" << std::endl;
			continue;
		}

		if ((attr = raidNode.attribute("file"))) {
			file = attr.as_string();
		} else {
			file = fmt::format("raids/{:s}.xml", name);
			std::cout << "[Warning - Raids::loadFromXml] File tag missing for raid " << name << ". Using default: " << file << std::endl;
		}

		if ((attr = raidNode.attribute("log"))) {
			log = attr.as_bool();
		}

		if ((attr = raidNode.attribute("date"))) {
			date = pugi::cast<time_t>(attr.value());
		} else if ((attr = raidNode.attribute("interval"))) {
			interval = pugi::cast<uint32_t>(attr.value());
		}

		if (interval == 0 && date == 0) {
			std::cout << "[Warning - Raids::loadFromXml] No date or interval set for raid " << name << "." << std::endl;
			continue;
		}

		Raid* newRaid = new Raid(name, interval);
		newRaid->setDateTime(date);
		newRaid->setLogged(log);
		if (newRaid->loadFromXml("data/raids/" + file)) {
			raidList.push_back(newRaid);
		} else {
			std::cout << "[Error - Raids::loadFromXml] Failed to load raid: " << name << std::endl;
			delete newRaid;
		}
	}

	time_t prevRaidDate = 0;
	for (auto it = raidList.begin(), end = raidList.end(); it != end; ++it) {
		Raid* raid = *it;

		Database& db = Database::getInstance();
		if (auto queryResult = db.storeQuery(fmt::format("SELECT `date` FROM `raids` WHERE `name` = {:s}", db.escapeString(raid->getName())))) {
			raid->setDateTime(queryResult->getNumber<time_t>("date"));
			if (raid->isLogged()) {
				std::cout << ">> [Raids] " << raid->getName() << " scheduled to happen somewhere around "
				          << formatDate(raid->getDateTime()) << std::endl;
			}
		} else {
			if (raid->getInterval() != 0) {
				// Execute the raid within 1 to 20 hours in 
				time_t executionDate =
				    std::time(nullptr) + raid->getInterval() + uniform_random(1 * 60 * 60, 20 * 60 * 60);
				raid->setDateTime(executionDate);
			}

			time_t currentDateTime = raid->getDateTime();
			if (prevRaidDate != 0 && raid->getDateTime() - prevRaidDate <= std::time(nullptr) + 24 * 60 * 60) {
				raid->setDateTime(raid->getDateTime() +
				                  uniform_random(3 * 24 * 60 * 60, 30 * 24 * 60 * 60));
			}

			prevRaidDate = currentDateTime;
			if (raid->isLogged()) {
				std::cout << ">> [Raids] " << raid->getName() << " scheduled to happen somewhere around "
				          << formatDate(raid->getDateTime()) << std::endl;
			}
			g_databaseTasks.addTask(fmt::format("INSERT INTO `raids`(`date`, `name`) VALUES ({:d}, {:s})",
			                                    raid->getDateTime(), db.escapeString(raid->getName())));
		}
	}

	loaded = true;
	return true;
}

bool Raids::startup()
{
	if (!isLoaded() || isStarted()) {
		return false;
	}

	checkRaidsEvent = g_scheduler.addEvent(createSchedulerTask(CHECK_RAIDS_INTERVAL, std::bind(&Raids::checkRaids, this)));

	started = true;
	return started;
}

void Raids::checkRaids()
{
	if (!getRunning()) {
		for (auto it = raidList.begin(), end = raidList.end(); it != end; ++it) {
			Raid* raid = *it;
			if (!raid->hasExecuted() && std::time(nullptr) >= raid->getDateTime()) {
				// Update the raid next execution date
				if (raid->getInterval() != 0) {
					raid->setDateTime(std::time(nullptr) + raid->getInterval() +
					                  normal_random(1 * 60 * 60, 20 * 60 * 60));

					if (raid->isLogged()) {
						std::cout << ">> [Raids] " << raid->getName() << " re-scheduled to happen somewhere around "
						          << formatDate(raid->getDateTime()) << std::endl;
					}
				}

				Database& db = Database::getInstance();
				g_databaseTasks.addTask(
				    fmt::format("UPDATE `raids` SET `date` = {:d}, `count` = `count` + 1 WHERE `name` = {:s}", raid->getDateTime(), db.escapeString(raid->getName())));

				setRunning(raid);
				raid->startRaid();
				raid->setExecuted();
				break;
			}
		}
	}

	checkRaidsEvent = g_scheduler.addEvent(createSchedulerTask(CHECK_RAIDS_INTERVAL, std::bind(&Raids::checkRaids, this)));
}

void Raids::clear()
{
	g_scheduler.stopEvent(checkRaidsEvent);
	checkRaidsEvent = 0;

	for (Raid* raid : raidList) {
		raid->stopEvents();
		delete raid;
	}
	raidList.clear();

	loaded = false;
	started = false;
	running = nullptr;

	scriptInterface.reInitState();
}

bool Raids::reload()
{
	clear();
	return loadFromXml();
}

Raid* Raids::getRaidByName(const std::string& name)
{
	for (Raid* raid : raidList) {
		if (strcasecmp(raid->getName().c_str(), name.c_str()) == 0) {
			return raid;
		}
	}
	return nullptr;
}

Raid::~Raid()
{
	for (RaidEvent* raidEvent : raidEvents) {
		delete raidEvent;
	}
}

bool Raid::loadFromXml(const std::string& filename)
{
	if (isLoaded()) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - Raid::loadFromXml", filename, result);
		return false;
	}

	for (auto eventNode : doc.child("raid").children()) {
		RaidEvent* event;
		if (strcasecmp(eventNode.name(), "announce") == 0) {
			event = new AnnounceEvent();
		} else if (strcasecmp(eventNode.name(), "singlespawn") == 0) {
			event = new SingleSpawnEvent();
		} else if (strcasecmp(eventNode.name(), "areaspawn") == 0) {
			event = new AreaSpawnEvent();
		} else if (strcasecmp(eventNode.name(), "script") == 0) {
			event = new ScriptEvent(&g_game.raids.getScriptInterface());
		} else {
			continue;
		}

		if (event->configureRaidEvent(eventNode)) {
			raidEvents.push_back(event);
		} else {
			std::cout << "[Error - Raid::loadFromXml] In file (" << filename << "), eventNode: " << eventNode.name() << std::endl;
			delete event;
		}
	}

	//sort by delay time
	std::sort(raidEvents.begin(), raidEvents.end(), [](const RaidEvent* lhs, const RaidEvent* rhs) {
		return lhs->getDelay() < rhs->getDelay();
	});

	loaded = true;
	return true;
}

void Raid::startRaid()
{
	RaidEvent* raidEvent = getNextRaidEvent();
	if (raidEvent) {
		nextEventEvent = g_scheduler.addEvent(createSchedulerTask(raidEvent->getDelay(), std::bind(&Raid::executeRaidEvent, this, raidEvent)));
	}
}

void Raid::executeRaidEvent(RaidEvent* raidEvent)
{
	if (raidEvent->executeEvent()) {
		nextEvent++;
		RaidEvent* newRaidEvent = getNextRaidEvent();

		if (newRaidEvent) {
			uint32_t ticks = static_cast<uint32_t>(std::max<int32_t>(1000, newRaidEvent->getDelay() - raidEvent->getDelay()));
			nextEventEvent = g_scheduler.addEvent(createSchedulerTask(ticks, std::bind(&Raid::executeRaidEvent, this, newRaidEvent)));
		} else {
			resetRaid();
		}
	} else {
		resetRaid();
	}
}

void Raid::resetRaid()
{
	nextEvent = 0;
	g_game.raids.setRunning(nullptr);
}

void Raid::stopEvents()
{
	if (nextEventEvent != 0) {
		g_scheduler.stopEvent(nextEventEvent);
		nextEventEvent = 0;
	}
}

RaidEvent* Raid::getNextRaidEvent()
{
	if (nextEvent < raidEvents.size()) {
		return raidEvents[nextEvent];
	} else {
		return nullptr;
	}
}

bool RaidEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	pugi::xml_attribute delayAttribute = eventNode.attribute("delay");
	if (!delayAttribute) {
		std::cout << "[Error] Raid: delay tag missing." << std::endl;
		return false;
	}

	delay = std::max<uint32_t>(1000, pugi::cast<uint32_t>(delayAttribute.value()));
	return true;
}

bool AnnounceEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute messageAttribute = eventNode.attribute("message");
	if (!messageAttribute) {
		std::cout << "[Error] Raid: message tag missing for announce event." << std::endl;
		return false;
	}
	message = messageAttribute.as_string();

	pugi::xml_attribute typeAttribute = eventNode.attribute("type");
	if (typeAttribute) {
		std::string tmpStrValue = asLowerCaseString(typeAttribute.as_string());
		if (tmpStrValue == "warning") {
			messageType = MESSAGE_STATUS_WARNING;
		} else if (tmpStrValue == "event") {
			messageType = MESSAGE_EVENT_ADVANCE;
		} else if (tmpStrValue == "default") {
			messageType = MESSAGE_EVENT_DEFAULT;
		} else if (tmpStrValue == "description") {
			messageType = MESSAGE_INFO_DESCR;
		} else if (tmpStrValue == "smallstatus") {
			messageType = MESSAGE_STATUS_SMALL;
		} else if (tmpStrValue == "blueconsole") {
			messageType = MESSAGE_STATUS_CONSOLE_BLUE;
		} else if (tmpStrValue == "redconsole") {
			messageType = MESSAGE_STATUS_CONSOLE_RED;
		} else {
			std::cout << "[Notice] Raid: Unknown type tag missing for announce event. Using default: " << static_cast<uint32_t>(messageType) << std::endl;
		}
	} else {
		messageType = MESSAGE_EVENT_ADVANCE;
		std::cout << "[Notice] Raid: type tag missing for announce event. Using default: " << static_cast<uint32_t>(messageType) << std::endl;
	}
	return true;
}

bool AnnounceEvent::executeEvent()
{
	g_game.broadcastMessage(message, messageType);
	return true;
}

bool SingleSpawnEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute attr;
	if ((attr = eventNode.attribute("name"))) {
		monsterName = attr.as_string();
	} else {
		std::cout << "[Error] Raid: name tag missing for singlespawn event." << std::endl;
		return false;
	}

	if ((attr = eventNode.attribute("x"))) {
		position.x = pugi::cast<uint16_t>(attr.value());
	} else {
		std::cout << "[Error] Raid: x tag missing for singlespawn event." << std::endl;
		return false;
	}

	if ((attr = eventNode.attribute("y"))) {
		position.y = pugi::cast<uint16_t>(attr.value());
	} else {
		std::cout << "[Error] Raid: y tag missing for singlespawn event." << std::endl;
		return false;
	}

	if ((attr = eventNode.attribute("z"))) {
		position.z = pugi::cast<uint16_t>(attr.value());
	} else {
		std::cout << "[Error] Raid: z tag missing for singlespawn event." << std::endl;
		return false;
	}

	for (auto lootNode : eventNode.children()) {
		LootBlock loot;

		if ((attr = lootNode.attribute("item"))) {
			loot.id = pugi::cast<uint16_t>(attr.value());
		}

		if ((attr = lootNode.attribute("countmax"))) {
			loot.countmax = pugi::cast<uint32_t>(attr.value());
		}

		if ((attr = lootNode.attribute("chance"))) {
			loot.chance = pugi::cast<uint32_t>(attr.value());
		}

		extraLoot.push_back(std::move(loot));
	}

	return true;
}

bool SingleSpawnEvent::executeEvent()
{
	Monster* monster = Monster::createMonster(monsterName, &extraLoot);
	if (!monster) {
		std::cout << "[Error] Raids: Cant create monster " << monsterName << std::endl;
		return false;
	}

	if (!g_game.placeCreature(monster, position, true)) {
		delete monster;
		std::cout << "[Error] Raids: Cant place monster " << monsterName << std::endl;
		return false;
	}

	g_game.addMagicEffect(monster->getPosition(), CONST_ME_TELEPORT);
	return true;
}

bool AreaSpawnEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute attr;
	if ((attr = eventNode.attribute("radius"))) {
		int32_t radius = pugi::cast<int32_t>(attr.value());
		Position centerPos;

		if ((attr = eventNode.attribute("centerx"))) {
			centerPos.x = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: centerx tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("centery"))) {
			centerPos.y = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: centery tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("centerz"))) {
			centerPos.z = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: centerz tag missing for areaspawn event." << std::endl;
			return false;
		}

		fromPos.x = std::max<int32_t>(0, centerPos.getX() - radius);
		fromPos.y = std::max<int32_t>(0, centerPos.getY() - radius);
		fromPos.z = centerPos.z;

		toPos.x = std::min<int32_t>(0xFFFF, centerPos.getX() + radius);
		toPos.y = std::min<int32_t>(0xFFFF, centerPos.getY() + radius);
		toPos.z = centerPos.z;
	} else {
		if ((attr = eventNode.attribute("fromx"))) {
			fromPos.x = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: fromx tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("fromy"))) {
			fromPos.y = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: fromy tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("fromz"))) {
			fromPos.z = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: fromz tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("tox"))) {
			toPos.x = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: tox tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("toy"))) {
			toPos.y = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: toy tag missing for areaspawn event." << std::endl;
			return false;
		}

		if ((attr = eventNode.attribute("toz"))) {
			toPos.z = pugi::cast<uint16_t>(attr.value());
		} else {
			std::cout << "[Error] Raid: toz tag missing for areaspawn event." << std::endl;
			return false;
		}
	}

	for (auto monsterNode : eventNode.children()) {
		const char* name;

		if ((attr = monsterNode.attribute("name"))) {
			name = attr.value();
		} else {
			std::cout << "[Error] Raid: name tag missing for monster node." << std::endl;
			return false;
		}

		uint32_t minAmount;
		if ((attr = monsterNode.attribute("minamount"))) {
			minAmount = pugi::cast<uint32_t>(attr.value());
		} else {
			minAmount = 0;
		}

		uint32_t maxAmount;
		if ((attr = monsterNode.attribute("maxamount"))) {
			maxAmount = pugi::cast<uint32_t>(attr.value());
		} else {
			maxAmount = 0;
		}

		uint32_t spread;
		if ((attr = monsterNode.attribute("spread"))) {
			spread = pugi::cast<uint32_t>(attr.value());
		} else {
			spread = 0;
		}

		uint64_t lifetime;
		if ((attr = monsterNode.attribute("lifetime"))) {
			lifetime = pugi::cast<uint64_t>(attr.value());
		} else {
			lifetime = 0;
		}

		if (maxAmount == 0 && minAmount == 0) {
			if ((attr = monsterNode.attribute("amount"))) {
				minAmount = pugi::cast<uint32_t>(attr.value());
				maxAmount = minAmount;
			} else {
				std::cout << "[Error] Raid: amount tag missing for monster node." << std::endl;
				return false;
			}
		}

		spawnList.emplace_back(name, minAmount, maxAmount, spread, lifetime);

		auto& backSpawn = spawnList.back();

		for (auto lootNode : monsterNode.children()) {
			LootBlock loot;

			if ((attr = lootNode.attribute("item"))) {
				loot.id = pugi::cast<uint16_t>(attr.value());
			}

			if ((attr = lootNode.attribute("countmax"))) {
				loot.countmax = pugi::cast<uint32_t>(attr.value());
			}

			if ((attr = lootNode.attribute("chance"))) {
				loot.chance = pugi::cast<uint32_t>(attr.value());
			}

			backSpawn.extraLoot.push_back(std::move(loot));
		}

	}
	return true;
}

bool AreaSpawnEvent::executeEvent()
{
	for (const MonsterSpawn& spawn : spawnList) {
		uint32_t amount = uniform_random(spawn.minAmount, spawn.maxAmount);
		for (uint32_t i = 0; i < amount; ++i) {
			Monster* monster = Monster::createMonster(spawn.name, &spawn.extraLoot);
			if (!monster) {
				std::cout << "[Error - AreaSpawnEvent::executeEvent] Can't create monster " << spawn.name << std::endl;
				return false;
			}

			if (spawn.lifetime > 0) {
				monster->setLifeTimeExpiration(OTSYS_TIME() + spawn.lifetime);
			}

			bool success = false;
			for (int32_t tries = 0; tries < MAXIMUM_TRIES_PER_MONSTER; tries++) {
				Tile* tile = g_game.map.getTile(uniform_random(fromPos.x, toPos.x), uniform_random(fromPos.y, toPos.y), uniform_random(fromPos.z, toPos.z));
				if (tile && !tile->isMoveableBlocking() && !tile->hasFlag(TILESTATE_PROTECTIONZONE) && tile->getTopCreature() == nullptr && g_game.placeCreature(monster, tile->getPosition(), true)) {
					success = true;
					g_game.addMagicEffect(monster->getPosition(), CONST_ME_TELEPORT);
					break;
				}
			}

			if (!success) {
				delete monster;
			}
		}
	}
	return true;
}

bool ScriptEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute scriptAttribute = eventNode.attribute("script");
	if (!scriptAttribute) {
		std::cout << "Error: [ScriptEvent::configureRaidEvent] No script file found for raid" << std::endl;
		return false;
	}

	if (!loadScript("data/raids/scripts/" + std::string(scriptAttribute.as_string()))) {
		std::cout << "Error: [ScriptEvent::configureRaidEvent] Can not load raid script." << std::endl;
		return false;
	}
	return true;
}

std::string ScriptEvent::getScriptEventName() const
{
	return "onRaid";
}

bool ScriptEvent::executeEvent()
{
	//onRaid()
	if (!scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - ScriptEvent::onRaid] Call stack overflow" << std::endl;
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	scriptInterface->pushFunction(scriptId);

	return scriptInterface->callFunction(0);
}
