#include "otpch.h"

#include "battlepass.h"
#include "tools.h"
#include "game.h"
#include "iologindata.h"

extern Game g_game;
static uint16_t m_lastQuestId = 0;

// How many quests will be assigned to player, default are 3 daily, 6 weekly and 0 special
// 0 - assign all quests from this category
static constexpr auto BATTLEPASS_DAILY_QUESTS_QUANTITY = 3;
static constexpr auto BATTLEPASS_WEEKLY_QUESTS_QUANTITY = 6;
static constexpr auto BATTLEPASS_SPECIAL_QUESTS_QUANTITY = 0;

// Battlepass quest cooldown
static constexpr auto BATTLEPASS_COOLDOWN_DAY = 24 * 60 * 60 * 1000;
static constexpr auto BATTLEPASS_COOLDOWN_WEEK = BATTLEPASS_COOLDOWN_DAY * 7;

// This function calculates the difficulty level based on player's level
// There are 5 difficulty levels described below
bool BattlePassData::canAssignQuest(uint32_t level) const
{
	/*
		0 - from level 1 to 45
		1 - from level 46 to 90
		2 - from level 91 to xxx
		4 - all levels
	*/
	switch (difficulty)
	{
		case 0:
			return level <= 45;
		case 1:
			return level >= 46 && level <= 90;
		case 2:
			return level >= 91; //&& level <= 120;
//		case 3:
//			return level >= 75;
		default:
			return true;
	}
}

// This function calculates how many experience per battlepass level player needs to get
const uint32_t BattlePasses::getExperiencePerLevel(uint32_t level)
{
	return 500 + ((level / 2.) * (2000 + ((level - 1) * 50)));
}

static struct I {
	const char* name;
	int type;
}
battlepasslua[] = {
	{ "difficulty", LUA_TNUMBER },
	{ "type", LUA_TNUMBER },
	{ "species", LUA_TNUMBER },
	{ "amount", LUA_TNUMBER },
	{ "experience", LUA_TNUMBER },
	{ "classId", LUA_TNUMBER },
	{ "name", LUA_TSTRING },
	{ nullptr, 0 }
};

void BattlePasses::loadRewards(lua_State* L)
{
	lua_getglobal(L, "battlepassrewards");
	for (uint16_t i = 1; ; i++)
	{
		lua_rawgeti(L, -1, i);
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			break;
		}

		auto extractRewards = [&](const char* text, BattlepassRewardsContainer* data)
		{
			lua_getfield(L, -1, text);
			if (lua_istable(L, -1))
			{
				lua_pushnil(L);
				while (lua_next(L, -2))
				{
					uint16_t rewardId = LuaScriptInterface::getFieldUnsigned(L, "itemId");
					uint16_t clientId = 0;
					uint16_t amount = LuaScriptInterface::getFieldUnsigned(L, "amount");
					BattlePassRewardType_t rewardType = BATTLEPASS_REWARD_PREMIUM;
					if (rewardId > 0)
					{
						rewardType = BATTLEPASS_REWARD_ITEM;
						clientId = Item::items.getItemType(rewardId).clientId;
					}
					else
					{
						rewardId = LuaScriptInterface::getFieldUnsigned(L, "outfitId");
						if (rewardId > 0)
						{
							rewardType = BATTLEPASS_REWARD_OUTFIT;
						}
						else
						{
							rewardId = LuaScriptInterface::getFieldUnsigned(L, "mountId");
							if (rewardId > 0)
							{
								rewardType = BATTLEPASS_REWARD_MOUNT;
							}
							else
							{
								rewardId = LuaScriptInterface::getFieldUnsigned(L, "wingsId");
								if (rewardId > 0)
								{
									rewardType = BATTLEPASS_REWARD_WINGS;
								}
								else
								{
									rewardId = LuaScriptInterface::getFieldUnsigned(L, "premium");
								}
							}
						}
					}

					assert(rewardId > 0);
					data->push_back({ rewardType, rewardId, clientId, amount });
					lua_pop(L, 1);
				}

				lua_pop(L, 1);
			}
		};

		BattlePassRewards rewards;
		extractRewards("freeReward", &rewards.freeRewards);
		extractRewards("premiumReward", &rewards.premiumRewards);

		m_battlePassRewards.emplace(i, rewards);
		lua_pop(L, 1);
	}
}

bool BattlePasses::load()
{
	lua_State* L = luaL_newstate();
	if (!L) {
		throw std::runtime_error("Failed to allocate memory in battlepass");
	}

	luaL_openlibs(L);
	LuaScriptInterface::registerEnums(L);

	if (luaL_dofile(L, "data/scripts/custom/battlepass.lua"))
	{
		std::cout << "[Error - battlepass] " << lua_tostring(L, -1) << std::endl;
		lua_close(L);
		return false;
	}

	loadRewards(L);

	lua_getglobal(L, "battlepassquests");
	for (uint16_t i = 1; ; i++)
	{
		lua_rawgeti(L, -1, i);
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			break;
		}

		lua_getfield(L, -1, "id");
		if (!lua_isnumber(L, -1))
		{
			std::cout << "[Warning - BattlePasses::load] Unknown battlepass id." << std::endl;
			return false;
		}

		BattlePassType_t questType = static_cast<BattlePassType_t>(lua_tonumber(L, -1));
		lua_pop(L, 1);

		lua_getfield(L, -1, "quests");
		if (!lua_istable(L, -1))
		{
			std::cout << "[Warning - BattlePasses::load] Missing quests node." << std::endl;
			return false;
		}

		lua_pushnil(L);
		while (lua_next(L, -2))
		{
			BattlePassData* battlePassData = new BattlePassData();
			BattlePassQuests_t questId = BATTLEPASS_QUEST_NONE;
			for (uint16_t j = 0; battlepasslua[j].name != nullptr; j++)
			{
				const std::string name = battlepasslua[j].name;
				const int type = battlepasslua[j].type;

				lua_getfield(L, -1, battlepasslua[j].name);
				if (type == LUA_TSTRING && lua_isstring(L, -1))
				{
					if (name == "name")
					{
						battlePassData->value = std::string(lua_tostring(L, -1));
					}
				}
				else if (type == LUA_TNUMBER && lua_isnumber(L, -1))
				{
					if (name == "difficulty")
					{
						battlePassData->difficulty = lua_tonumber(L, -1);
					}
					else if (name == "type")
					{
						questId = static_cast<BattlePassQuests_t>(lua_tonumber(L, -1));
					}
					else if (name == "amount")
					{
						battlePassData->amount = lua_tonumber(L, -1);
					}
					else if (name == "experience")
					{
						battlePassData->experience = lua_tonumber(L, -1);
					}
					else if (name == "species" || name == "classId")
					{
						battlePassData->value = static_cast<uint16_t>(lua_tonumber(L, -1));
					}
				}

				lua_pop(L, 1);
			}

			if (questId == BATTLEPASS_QUEST_NONE)
			{
				std::cout << "[Error - BattlePasses::load] Invalid battlepass quest's type." << std::endl;
				return false;
			}

			battlePassData->id = ++m_lastQuestId;
			m_questsId[questType].push_back({ m_lastQuestId, questId, battlePassData });
			lua_pop(L, 1);
		}

		lua_pop(L, 1);
		lua_pop(L, 1);
	}

	lua_close(L);
	return true;
}

bool BattlePasses::reload()
{
	m_questsId.clear();
	m_battlePassRewards.clear();
	return load();
}

BattlePass::BattlePass()
{

}

BattlePass::~BattlePass()
{

}

void BattlePass::clear()
{
	m_battlepass.clear();
	m_experience = 0;
}

void BattlePass::reset(uint32_t level)
{
	clear();

	resetQuests(BATTLEPASS_TYPE_DAILY, BATTLEPASS_DAILY_QUESTS_QUANTITY, level);
	resetQuests(BATTLEPASS_TYPE_WEEKLY, BATTLEPASS_WEEKLY_QUESTS_QUANTITY, level);
	resetQuests(BATTLEPASS_TYPE_SPECIAL, BATTLEPASS_SPECIAL_QUESTS_QUANTITY, level);
}

bool BattlePass::update(const BattlePassQuests_t id, const std::any& value, BattlePassQuestsVector* quests)
{
	return incrementCounter(id, value, quests);
}

void BattlePass::resetQuests(const BattlePassType_t type, const uint8_t limit, const uint32_t level)
{
	auto& data = BattlePasses::getInstance()->getQuests();
	auto itDataType = data.find(type);
	if (itDataType == data.end())
	{
		std::cout << "[Error - BattlePass::resetQuests]: Cannot find any battlepass quests for type ID " << type << "." << std::endl;
		return;
	}

	if (limit == 0)
	{
		// If the limit is set to 0 it means we want to start all quests from the poll
		for (auto& itBattlepassQuests : itDataType->second)
		{
			addQuest(type, std::get<1>(itBattlepassQuests), 0, 0, std::get<2>(itBattlepassQuests)->value, false, std::get<2>(itBattlepassQuests));
		}
		
		return;
	}

	// Get an empty battlepass data
	uint8_t counter = 0;
	while (counter < limit)
	{
		auto index = normal_random(0, itDataType->second.size() - 1);
		auto& itBattlepassQuests = itDataType->second.at(index);
		
		// Check if we've already considered this quest
		auto battlepassQuest = std::get<2>(itBattlepassQuests);
		if (std::find(m_questsId.begin(), m_questsId.end(), battlepassQuest->id) != m_questsId.end())
		{
			continue;
		}

		if (battlepassQuest->canAssignQuest(level))
		{
			addQuest(type, std::get<1>(itBattlepassQuests), 0, 0, battlepassQuest->value, false, battlepassQuest);
			counter++;
		}
	}
}

BattlePassPlayerData* BattlePass::getQuest(uint16_t id)
{
	for (auto& itBattlePassType : m_battlepass)
	{
		for (auto& itBattlePassId : itBattlePassType.second)
		{
			if (itBattlePassId.data->id == id)
			{
				return &itBattlePassId;
			}
		}
	}

	std::cout << "[Error - BattlePass::getQuest]: Cannot find any battlepass quests with id '" << id << "'." << std::endl;
	return nullptr;
}

const BattlePassData* BattlePass::getData(const BattlePassType_t type, const BattlePassQuests_t id, const std::any& value)
{
	auto& data = BattlePasses::getInstance()->getQuests();
	auto itDataType = data.find(type);
	if (itDataType == data.end())
	{
		std::cout << "[Error - BattlePass::getData]: Cannot find any battlepass quests for type '" << type << "'." << std::endl;
		return nullptr;
	}

	const auto& valueType = value.type();
	for (auto& itBattlepass : itDataType->second)
	{
		if (id != std::get<1>(itBattlepass))
		{
			// The quest ids don't match
			continue;
		}

		auto battlepassQuest = std::get<2>(itBattlepass);
		const auto& bpValueType = battlepassQuest->value.type();
		if (!value.has_value() && !battlepassQuest->value.has_value())
		{
			return battlepassQuest;
		}

		if (valueType == bpValueType)
		{
			if ((valueType == typeid(uint16_t) && std::any_cast<uint16_t>(value) == std::any_cast<uint16_t>(battlepassQuest->value)) ||
				(valueType == typeid(std::string) && std::any_cast<std::string>(value) == std::any_cast<std::string>(battlepassQuest->value)))
			{
				return battlepassQuest;
			}
		}
	}

	std::cout << "[Error - BattlePass::getData]: Cannot find any battlepass quests for value." << std::endl;
	return nullptr;
}

bool BattlePass::getData(const BattlePassQuests_t id, BattlePassQuestsVector& data)
{
	const auto timeNow = OTSYS_TIME();
	for(uint8_t type = BATTLEPASS_TYPE_FIRST; type <= BATTLEPASS_TYPE_LAST; ++type)
	{
		const auto itBattlepassId = m_battlepass.find(static_cast<BattlePassType_t>(type));
		if (itBattlepassId == m_battlepass.end())
		{
			// Cannot find active quest with this id
			continue;
		}

		auto& quests = itBattlepassId->second;
		for (auto& itQuest : quests)
		{
			if (itQuest.id != id)
			{
				continue;
			}

			if (itQuest.cooldown > 1 && itQuest.cooldown >= timeNow)
			{
				// There is still a cooldown to this quest
				// If the cooldown is set to 0 (quest is not finished) or 1 (quest is finished with cooldown) ignore this quest
				continue;
			}

			data.push_back(&itQuest);
		}
	}

	return !data.empty();
}

void BattlePass::addQuest(const BattlePassType_t type, const BattlePassQuests_t id, const uint32_t amount, const time_t cooldown, const std::any& value, bool shuffled, const BattlePassData* data)
{
	if (!data)
	{
		// Quests loaded from iologindata have no data set, we need to find one
		data = getData(type, id, value);
		if (!data)
		{
			std::cout << "[Error - BattlePass::addQuest]: The data has not been found for quest id " << id << ", type " << type << ", value " << value.type().name() << "." << std::endl;
			return;
		}
	}

	auto battlepassEntry = BattlePassPlayerData(id, type, cooldown, amount, shuffled, data);
	auto itBattlepassType = m_battlepass.find(type);
	if (itBattlepassType == m_battlepass.end())
	{
		// There are no quests with this type
		m_battlepass.emplace(type, BattlePassQuests({ battlepassEntry }));
	}
	else
	{
		itBattlepassType->second.push_back(battlepassEntry);
	}

	// Remember that we've considered this quest
	m_questsId.push_back(data->id);
}

bool BattlePass::checkQuest(BattlePassPlayerData* data)
{
	if (data->amount == data->data->amount)
	{
		// No need to increase the value over the maximum limit
		return false;
	}

	data->amount++;
	return true;
}

void BattlePass::addRewards(Player* player) const
{
	auto& rewards = BattlePasses::getInstance()->getRewards();
	auto itRewards = rewards.find(m_level);
	if (itRewards == rewards.end())
	{
		std::cout << "[Error - BattlePass::addRewards]: Cannot find any battlepass rewards for level '" << m_level << "'." << std::endl;
		return;
	}

	if (!hasPremium() && itRewards->second.freeRewards.empty())
	{
		// Players with no premium account don't get rewards if there are no rewards for free account players
		return;
	}

	Item* mailItem = nullptr;
	for (auto& itReward : itRewards->second.freeRewards) {
		// Free rewards
		switch (std::get<0>(itReward)) {
			case BATTLEPASS_REWARD_ITEM: {
				uint16_t itemId = std::get<1>(itReward);
				uint16_t count = std::get<3>(itReward);

				Item* item = Item::CreateItem(itemId, count);
				if (item) {
					item->setStoreItem(true);

					StoreInbox* inbox = player->getStoreInbox();
					if (inbox) {
						inbox->addItem(item);
						player->sendTextMessage(MESSAGE_EVENT_ADVANCE, "BattlePass reward delivered to your Store Inbox!");
					} else {
						player->sendTextMessage(MESSAGE_EVENT_DEFAULT, "Failed to deliver BattlePass reward.");
						delete item;
					}

					item->setStoreItem(false);
				}
				break;
			}
			case BATTLEPASS_REWARD_OUTFIT: {
				player->addOutfit(std::get<1>(itReward), std::get<3>(itReward));
				break;
			}
			case BATTLEPASS_REWARD_MOUNT: {
				//player->tameMount(std::get<1>(itReward));
				break;
			}
			case BATTLEPASS_REWARD_WINGS: {
				//player->addWings(std::get<1>(itReward));
				break;
			}
			case BATTLEPASS_REWARD_PREMIUM: {
				player->setPremiumTime(player->getPremiumEndsAt() + (std::get<1>(itReward) * (24 * 60 * 60)));
				break;
			}
		}
	}

	if (hasPremium()) {
		// Premium rewards
		for (auto& itReward : itRewards->second.premiumRewards) {
			// Free rewards
			switch (std::get<0>(itReward)) {
				case BATTLEPASS_REWARD_ITEM: {
					uint16_t itemId = std::get<1>(itReward);
					uint16_t count = std::get<3>(itReward);

					Item* item = Item::CreateItem(itemId, count);
					if (item) {
						item->setStoreItem(true); 

						StoreInbox* inbox = player->getStoreInbox();
						if (inbox) {
							inbox->addItem(item);
							player->sendTextMessage(MESSAGE_EVENT_ADVANCE, "BattlePass reward delivered to your Store Inbox!");
						} else {
							player->sendTextMessage(MESSAGE_EVENT_DEFAULT, "Failed to deliver BattlePass reward.");
							delete item; 
						}

						item->setStoreItem(false);
					}
					break;
				}
				case BATTLEPASS_REWARD_OUTFIT: {
					player->addOutfit(std::get<1>(itReward), std::get<3>(itReward));
					break;
				}
				case BATTLEPASS_REWARD_MOUNT: {
					//player->tameMount(std::get<1>(itReward));
					break;
				}
				case BATTLEPASS_REWARD_WINGS: {
					//player->addWings(std::get<1>(itReward));
					break;
				}
				case BATTLEPASS_REWARD_PREMIUM: {
					player->setPremiumTime(player->getPremiumEndsAt() + (std::get<1>(itReward) * (24 * 60 * 60)));
					break;
				}
			}
		}
	}

	if(mailItem) {
		g_game.internalAddItem(player, mailItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
	}
}

void BattlePass::calculateExperience(Player* player, uint32_t experience)
{
	m_experience += experience;

	uint32_t nextLevelExperience = BattlePasses::getInstance()->getExperiencePerLevel(m_level);
	while (m_experience >= nextLevelExperience) {
		m_level++;
		nextLevelExperience = BattlePasses::getInstance()->getExperiencePerLevel(m_level);
		addRewards(player);
	}
}

bool BattlePass::resetQuest(uint32_t level, BattlePassPlayerData* data)
{
	const uint16_t currentId = data->data->id;
	auto itQuest = std::find(m_questsId.begin(), m_questsId.end(), currentId);
	if (itQuest == m_questsId.end()) {
		std::cout << "[Error - BattlePass::resetQuest]: Quest id '" << currentId << "' has not been found." << std::endl;
		return false;
	}

	auto& quests = BattlePasses::getInstance()->getQuests();
	auto itDataType = quests.find(data->type);
	if (itDataType == quests.end()) {
		std::cout << "[Error - BattlePass::resetQuest]: Cannot find any battlepass quests for type ID " << data->type << "." << std::endl;
		return false;
	}

	while (true) {
		auto index = normal_random(0, itDataType->second.size() - 1);
		auto& itBattlepassQuests = itDataType->second.at(index);

		// Check if we've already considered this quest
		auto battlepassQuest = std::get<2>(itBattlepassQuests);
		if (std::find(m_questsId.begin(), m_questsId.end(), battlepassQuest->id) != m_questsId.end()) {
			continue;
		}

		if (battlepassQuest->canAssignQuest(level)) {
			// Erase current quest id from the list
			m_questsId.erase(itQuest);

			data->amount = 0;
			data->cooldown = 0;
			data->shuffled = false;
			data->id = std::get<1>(itBattlepassQuests);
			data->data = battlepassQuest;

			// Add new quest id to the list
			m_questsId.push_back(data->data->id);
			return true;
		}
	}

	return false;
}

void BattlePass::checkQuestsCooldown(uint32_t level)
{
	const auto timeNow = OTSYS_TIME();
	for (auto& itBattlepass : m_battlepass) {
		for (auto& itBattlepassQuests : itBattlepass.second) {
			if (itBattlepassQuests.cooldown > 1 && itBattlepassQuests.cooldown < timeNow) {
				// If the cooldown is set to 1 it means the quest is completed and cannot be repeated
				resetQuest(level, &itBattlepassQuests);
			}
		}
	}
}

bool BattlePass::shuffleQuest(uint32_t level, uint16_t id, BattlePassPlayerData& data)
{
	for (auto& itBattlepass : m_battlepass) {
		for (auto& itBattlepassQuests : itBattlepass.second) {
			if (itBattlepassQuests.data->id == id) {
				if (itBattlepassQuests.shuffled) {
					// The quest is already shuffled
					return false;
				}

				// The quest has been found, reset it if not shuffled yet
				if (resetQuest(level, &itBattlepassQuests)) {
					data = itBattlepassQuests;
					itBattlepassQuests.shuffled = true;
					return true;
				}

				return false;
			}
		}
	}

	return false;
}

bool BattlePass::completeQuest(Player* player, uint16_t id, time_t* cooldown)
{
	BattlePassPlayerData* quest = getQuest(id);
	if (!quest) {
		return false;
	}

	if (quest->amount < quest->data->amount) {
		// Quest is not completed yet
		return false;
	}

	if (quest->cooldown > 0) {
		// Quest is already completed and is on its cooldown
		return false;
	}

	// We completed the quest, add experience and set the cooldown
	const auto timeNow = OTSYS_TIME();
	if(quest->type == BATTLEPASS_TYPE_DAILY) {
		// Set 1 day of cooldown to this quest
		quest->cooldown = timeNow + BATTLEPASS_COOLDOWN_DAY;
		*cooldown = BATTLEPASS_COOLDOWN_DAY;
	}
	else if (quest->type == BATTLEPASS_TYPE_WEEKLY) {
		// Set 7 days of cooldown to this quest
		quest->cooldown = timeNow + BATTLEPASS_COOLDOWN_WEEK;
		*cooldown = BATTLEPASS_COOLDOWN_WEEK;
	}
	else {
		// Set the cooldown to 1 to block this quest for future
		quest->cooldown = 1;
		*cooldown = 1;
	}

	calculateExperience(player, quest->data->experience);
	return true;
}

bool BattlePass::incrementCounter(const BattlePassQuests_t id, const std::any& value, BattlePassQuestsVector* quests)
{
	BattlePassQuestsVector data;
	if (!getData(id, data)) {
		return false;
	}

	const auto& valueType = value.type();
	const bool hasValue = value.has_value();
	for (auto& itData : data) {
		bool enabled = !hasValue;
		if (!enabled && valueType == itData->data->value.type()) {
			if ((valueType == typeid(uint16_t) && std::any_cast<uint16_t>(value) == std::any_cast<uint16_t>(itData->data->value)) ||
				(valueType == typeid(std::string) && std::any_cast<std::string>(value) == std::any_cast<std::string>(itData->data->value))) {
				enabled = true;
			}
		}

		if (enabled && checkQuest(itData)) {
			quests->push_back(itData);
		}
	}

	return !quests->empty();
}
