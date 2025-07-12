#pragma once

#include "luascript.h"
#include <map>

struct BattlePassData
{
	std::any value;
	uint8_t difficulty;
	uint16_t id;
	uint32_t amount, experience;

	bool canAssignQuest(uint32_t level) const;
};

static constexpr auto BATTLEPASS_PREMIUM_COIN_ID = 2148;
static constexpr auto BATTLEPASS_SHUFFLE_COST = 10;
static constexpr auto BATTLEPASS_PREMIUM_COST = 200;

//							   type					   itemId,	 clientId, amount
typedef std::vector<std::tuple<BattlePassRewardType_t, uint16_t, uint16_t, uint16_t>> BattlepassRewardsContainer;
struct BattlePassRewards
{
	BattlepassRewardsContainer freeRewards, premiumRewards;
};

typedef std::map<uint16_t, BattlePassRewards> BattlePassRewardsMap;
typedef std::map<BattlePassType_t, std::vector<std::tuple<uint16_t, BattlePassQuests_t, BattlePassData*>>> BattlePassQuestsMap;

class BattlePasses
{
	public:
		static BattlePasses* getInstance()
		{
			static BattlePasses instance;
			return &instance;
		}

		bool load();
		bool reload();

		void loadRewards(lua_State* L);

		const BattlePassQuestsMap& getQuests()		const { return m_questsId; }
		const BattlePassRewardsMap& getRewards()	const { return m_battlePassRewards; }
		
		static const uint32_t getExperiencePerLevel(uint32_t);

	private:
		BattlePassQuestsMap m_questsId;
		BattlePassRewardsMap m_battlePassRewards;
};

struct BattlePassPlayerData
{
	BattlePassPlayerData() : data(nullptr), id(BATTLEPASS_QUEST_NONE), type(BATTLEPASS_TYPE_NONE), cooldown(0), amount(0), shuffled(false) {}
	BattlePassPlayerData(BattlePassQuests_t _id, BattlePassType_t _type, time_t _cooldown, uint32_t _amount, bool _shuffled, const BattlePassData* _data):
		data(_data), id(_id), type(_type), cooldown(_cooldown), amount(_amount), shuffled(_shuffled) {}

	const BattlePassData* data;
	BattlePassQuests_t id;
	BattlePassType_t type;
	time_t cooldown;
	uint32_t amount;
	bool shuffled;
};

typedef std::vector<BattlePassPlayerData*> BattlePassQuestsVector;
typedef std::vector<BattlePassPlayerData> BattlePassQuests;
typedef std::map<BattlePassType_t, BattlePassQuests> BattlePassPlayerDataMap;
class BattlePass
{
	public:
		BattlePass();
		virtual ~BattlePass();

		void setExperience(uint32_t experience) { m_experience = experience; }
		void setLevel(uint16_t level)			{ m_level = level; }
		void setPremium(bool premium)			{ m_hasPremium = premium; }
		void addQuest(const BattlePassType_t, const BattlePassQuests_t, const uint32_t, const time_t, const std::any&, bool, const BattlePassData*);
		void reset(uint32_t);
		void checkQuestsCooldown(uint32_t);

		bool shuffleQuest(uint32_t, uint16_t, BattlePassPlayerData&);
		bool update(const BattlePassQuests_t, const std::any&, BattlePassQuestsVector*);
		bool completeQuest(Player*, uint16_t, time_t*);
		bool hasPremium()			const { return m_hasPremium; }

		uint32_t getExperience()	const { return m_experience; }
		uint16_t getLevel()			const { return m_level; }

		BattlePassPlayerDataMap& getBattlepass() { return m_battlepass; }

	private:
		void clear();
		void addRewards(Player*) const;
		void calculateExperience(Player*, uint32_t);
		void resetQuests(const BattlePassType_t, const uint8_t, const uint32_t);
		
		bool resetQuest(uint32_t, BattlePassPlayerData*);
		bool incrementCounter(const BattlePassQuests_t, const std::any&, BattlePassQuestsVector*);
		bool checkQuest(BattlePassPlayerData*);
		bool getData(const BattlePassQuests_t, BattlePassQuestsVector&);

		BattlePassPlayerData* getQuest(uint16_t);
		static const BattlePassData* getData(const BattlePassType_t, const BattlePassQuests_t, const std::any&);

		std::vector<uint16_t> m_questsId;
		BattlePassPlayerDataMap m_battlepass;
		uint16_t m_level = 0;
		uint32_t m_experience = 0;
		bool m_hasPremium = false;
};
