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

#include "baseevents.h"

#include "pugicast.h"
#include "tools.h"

extern LuaEnvironment g_luaEnvironment;

bool BaseEvents::loadFromXml()
{
	if (loaded) {
		std::cout << "[Error - BaseEvents::loadFromXml] It's already loaded." << std::endl;
		return false;
	}

	std::string scriptsName = getScriptBaseName();
	std::string basePath = "data/" + scriptsName + "/";
	if (getScriptInterface().loadFile(basePath + "lib/" + scriptsName + ".lua") == -1) {
		std::cout << "[Warning - BaseEvents::loadFromXml] Can not load " << scriptsName << " lib/" << scriptsName << ".lua" << std::endl;
	}

	std::string filename = basePath + scriptsName + ".xml";

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - BaseEvents::loadFromXml", filename, result);
		return false;
	}

	loaded = true;

	for (auto node : doc.child(scriptsName.c_str()).children()) {
		Event_ptr event = getEvent(node.name());
		if (!event) {
			continue;
		}

		if (!event->configureEvent(node)) {
			std::cout << "[Warning - BaseEvents::loadFromXml] Failed to configure event" << std::endl;
			continue;
		}

		bool success;

		pugi::xml_attribute scriptAttribute = node.attribute("script");
		if (scriptAttribute) {
			std::string scriptFile = "scripts/" + std::string(scriptAttribute.as_string());
			success = event->checkScript(basePath, scriptsName, scriptFile) && event->loadScript(basePath + scriptFile);
			if (node.attribute("function")) {
				event->loadFunction(node.attribute("function"), true);
			}
		} else {
			success = event->loadFunction(node.attribute("function"), false);
		}

		if (success) {
			registerEvent(std::move(event), node);
		}
	}
	return true;
}

bool BaseEvents::reload()
{
	loaded = false;
	clear(false);
	return loadFromXml();
}

void BaseEvents::reInitState(bool fromLua)
{
	if (!fromLua) {
		getScriptInterface().reInitState();
	}
}

Event::Event(LuaScriptInterface* interface) : scriptInterface(interface) {}

bool Event::checkScript(const std::string& basePath, const std::string& scriptsName, const std::string& scriptFile) const
{
	LuaScriptInterface* testInterface = g_luaEnvironment.getTestInterface();
	testInterface->reInitState();

	if (testInterface->loadFile(std::string(basePath + "lib/" + scriptsName + ".lua")) == -1) {
		std::cout << "[Warning - Event::checkScript] Can not load " << scriptsName << " lib/" << scriptsName << ".lua" << std::endl;
	}

	if (scriptId != 0) {
		std::cout << "[Failure - Event::checkScript] scriptid = " << scriptId << std::endl;
		return false;
	}

	if (testInterface->loadFile(basePath + scriptFile) == -1) {
		std::cout << "[Warning - Event::checkScript] Can not load script: " << scriptFile << std::endl;
		std::cout << testInterface->getLastLuaError() << std::endl;
		return false;
	}

	int32_t id = testInterface->getEvent(getScriptEventName());
	if (id == -1) {
		std::cout << "[Warning - Event::checkScript] Event " << getScriptEventName() << " not found. " << scriptFile << std::endl;
		return false;
	}
	return true;
}

bool Event::loadScript(const std::string& scriptFile)
{
	if (!scriptInterface || scriptId != 0) {
		std::cout << "Failure: [Event::loadScript] scriptInterface == nullptr. scriptFile = " << scriptFile << std::endl;
		return false;
	}

	if (scriptInterface->loadFile(scriptFile) == -1) {
		std::cout << "[Warning - Event::loadScript] Can not load script. " << scriptFile << std::endl;
		std::cout << scriptInterface->getLastLuaError() << std::endl;
		return false;
	}

	int32_t id = scriptInterface->getEvent(getScriptEventName());
	if (id == -1) {
		std::cout << "[Warning - Event::loadScript] Event " << getScriptEventName() << " not found. " << scriptFile << std::endl;
		return false;
	}

	scripted = true;
	scriptId = id;
	return true;
}

bool Event::loadCallback()
{
	if (!scriptInterface || scriptId != 0) {
		std::cout << "Failure: [Event::loadCallback] scriptInterface == nullptr. scriptid = " << scriptId << std::endl;
		return false;
	}

	int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		std::cout << "[Warning - Event::loadCallback] Event " << getScriptEventName() << " not found. " << std::endl;
		return false;
	}

	scripted = true;
	scriptId = id;
	return true;
}

bool CallBack::loadCallBack(LuaScriptInterface* interface, const std::string& name)
{
	if (!interface) {
		std::cout << "Failure: [CallBack::loadCallBack] scriptInterface == nullptr" << std::endl;
		return false;
	}

	scriptInterface = interface;

	int32_t id = scriptInterface->getEvent(name.c_str());
	if (id == -1) {
		std::cout << "[Warning - CallBack::loadCallBack] Event " << name << " not found." << std::endl;
		return false;
	}

	scriptId = id;
	loaded = true;
	return true;
}
