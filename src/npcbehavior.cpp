#include "otpch.h"

#include "npcbehavior.h"
#include "npc.h"
#include "player.h"
#include "game.h"
#include "spells.h"
#include "monster.h"
#include "scheduler.h"
#include "logger.h"

extern Game g_game;
extern Monsters g_monsters;
extern Spells* g_spells;

NpcBehavior::NpcBehavior(Npc* _npc) : npc(_npc) 
{
	topic = 0;
	data = -1;
	type = 0;
	price = 0;
	amount = 0;
	talkDelay = 1000;
}

NpcBehavior::~NpcBehavior() 
{
	for (NpcBehaviour* behaviour : behaviourEntries) {
		delete behaviour;
	}
}

void NpcBehavior::parseShop()
{
	std::string tempMessage;

	ShopInfoList tempList;
	Player player(nullptr);
	for (NpcBehaviour* behaviour : behaviourEntries) {
		bool hasBuyMessage = false;
		bool hasSellMessage = false;
		int item = 0;
		int price = 0;
		int subtype = 0;
		std::string lastBuyMsg;
		std::string lastSellMsg;
		ShopInfo info;

		for (const NpcBehaviourAction* action : behaviour->actions) {
			if (action->type == BEHAVIOUR_TYPE_STRING) {
				if (action->string.find("buy") != std::string::npos) {
					lastBuyMsg = action->string;
					hasBuyMessage = true;
				}

				if (action->string.find("sell") != std::string::npos) {
					lastSellMsg = action->string;
					hasSellMessage = true;
				}
			}

			if (action->type == BEHAVIOUR_TYPE_ITEM) {
				item = evaluate(action->expression, &player, tempMessage);
			}

			if (action->type == BEHAVIOUR_TYPE_PRICE) {
				price = evaluate(action->expression, &player, tempMessage);
			}

			if (action->type == BEHAVIOUR_TYPE_DATA) {
				subtype = evaluate(action->expression, &player, tempMessage);
			}
		}

		info.itemId = item;
		info.subType = subtype;
		info.realName = Item::items.getItemType(item).name;

		if (hasBuyMessage && price > 0 && item != 0) {
			info.buyPrice = price;
			tempList.push_back(info);
		}

		if (hasSellMessage && price > 0 && item != 0) {
			info.sellPrice = price;
			tempList.push_back(info);
		}
	}

	npc->cipShopList = tempList;
}

bool NpcBehavior::loadDatabase(const std::string& filename)
{
	ScriptReader script;
	if (!script.loadScript(filename)) {
		return false;
	}

	if (script.readIdentifier() != "behavior") {
		script.error("'behavior' expected");
		return false;
	}

	script.readSymbol('=');
	script.readSymbol('{');
	script.nextToken();

	while (script.canRead()) {
		if (script.getToken() == TOKEN_ENDOFFILE) {
			break;
		}

		if (script.getToken() == TOKEN_SPECIAL && script.getSpecial() == '}') {
			break;
		}

		if (!loadBehaviour(script)) {
			return false;
		}
	}

	return true;
}

bool NpcBehavior::loadBehaviour(ScriptReader& script)
{
	NpcBehaviour* behaviour = new NpcBehaviour();

	if (!loadConditions(script, behaviour)) {
		delete behaviour;
		return false;
	}

	if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != 'I') {
		script.error("'->' expected");
		delete behaviour;
		return false;
	}

	script.nextToken();
	if (!loadActions(script, behaviour)) {
		delete behaviour;
		return false;
	}

	// set this behaviour priority to condition size
	behaviour->priority += behaviour->conditions.size();

	if (priorityBehaviour) {
		priorityBehaviour->priority += behaviour->priority + 1;
		priorityBehaviour = nullptr;
	}

	// order it correctly
	auto it = std::lower_bound(behaviourEntries.begin(), behaviourEntries.end(), behaviour, compareBehaviour);
	behaviourEntries.insert(it, behaviour);

	// set previous behaviour (*) functionality
	previousBehaviour = behaviour;
	return true;
}

bool NpcBehavior::loadConditions(ScriptReader& script, NpcBehaviour* behaviour)
{
	while (script.canRead()) {
		std::unique_ptr<NpcBehaviourCondition> condition(new NpcBehaviourCondition);

		bool searchTerm = false;
		if (script.getToken() == TOKEN_IDENTIFIER) {
			std::string identifier = script.getIdentifier();
			if (identifier == "address") {
				condition->situation = SITUATION_ADDRESS;
				behaviour->situation = SITUATION_ADDRESS;
				searchTerm = true;
			} else if (identifier == "busy") {
				condition->situation = SITUATION_BUSY;
				behaviour->situation = SITUATION_BUSY;
				searchTerm = true;
			} else if (identifier == "vanish") {
				condition->situation = SITUATION_VANISH;
				behaviour->situation = SITUATION_VANISH;
				searchTerm = true;
			} else if (identifier == "sorcerer") {
				condition->type = BEHAVIOUR_TYPE_SORCERER;
				searchTerm = true;
			} else if (identifier == "knight") {
				condition->type = BEHAVIOUR_TYPE_KNIGHT;
				searchTerm = true;
			} else if (identifier == "paladin") {
				condition->type = BEHAVIOUR_TYPE_PALADIN;
				searchTerm = true;
			} else if (identifier == "druid") {
				condition->type = BEHAVIOUR_TYPE_DRUID;
				searchTerm = true;
			} else if (identifier == "premium") {
				condition->type = BEHAVIOUR_TYPE_ISPREMIUM;
				searchTerm = true;
			} else if (identifier == "pvpenforced") {
				condition->type = BEHAVIOUR_TYPE_PVPENFORCED;
				searchTerm = true;
			} else if (identifier == "female") {
				condition->type = BEHAVIOUR_TYPE_FEMALE;
				searchTerm = true;
			} else if (identifier == "male") {
				condition->type = BEHAVIOUR_TYPE_MALE;
				searchTerm = true;
			} else if (identifier == "pzblock") {
				condition->type = BEHAVIOUR_TYPE_PZLOCKED;
				searchTerm = true;
			} else if (identifier == "promoted") {
				condition->type = BEHAVIOUR_TYPE_PROMOTED;
				searchTerm = true;
			}
		} else if (script.getToken() == TOKEN_STRING) {
			const std::string keyString = asLowerCaseString(script.getString());
			condition->setCondition(BEHAVIOUR_TYPE_STRING, 0, keyString);

			searchTerm = true;

			behaviour->priority += 1;
		} else if (script.getToken() == TOKEN_SPECIAL) {
			if (script.getSpecial() == '!') {
				condition->setCondition(BEHAVIOUR_TYPE_NOP, 0, "");
				searchTerm = true;

				// set this one for behaviour
				priorityBehaviour = behaviour;
			} else if (script.getSpecial() == '%') {
				condition->setCondition(BEHAVIOUR_TYPE_MESSAGE_COUNT, script.readNumber(), "");
				searchTerm = true;
			} else if (script.getSpecial() == ',') {
				script.nextToken();
				continue;
			} else {
				break;
			}
		}

		// relational operation search
		if (!searchTerm) {
			condition->type = BEHAVIOUR_TYPE_OPERATION;
			NpcBehaviourNode* headNode = readValue(script);
			NpcBehaviourNode* nextNode = readFactor(script, headNode);
			
			behaviour->priority += 1;

			// relational operators
			if (script.getToken() != TOKEN_SPECIAL) {
				script.error("relational operator expected");
				delete nextNode;
				return false;
			}

			NpcBehaviourOperator_t operatorType;
			switch (script.getSpecial()) {
				case '<':
					operatorType = BEHAVIOUR_OPERATOR_LESSER_THAN;
					break;
				case '=':
					operatorType = BEHAVIOUR_OPERATOR_EQUALS;
					break;
				case '>':
					operatorType = BEHAVIOUR_OPERATOR_GREATER_THAN;
					break;
				case 'G':
					operatorType = BEHAVIOUR_OPERATOR_GREATER_OR_EQUALS;
					break;
				case 'N':
					operatorType = BEHAVIOUR_OPERATOR_NOT_EQUALS;
					break;
				case 'L':
					operatorType = BEHAVIOUR_OPERATOR_LESSER_OR_EQUALS;
					break;
				default:
					script.error("relational operator expected");
					delete nextNode;
					return false;
			}

			script.nextToken();
			headNode = new NpcBehaviourNode();
			headNode->type = BEHAVIOUR_TYPE_OPERATION;
			headNode->number = operatorType;
			headNode->left = nextNode;
			nextNode = readValue(script);
			nextNode = readFactor(script, nextNode);
			headNode->right = nextNode;

			condition->expression = headNode;
		} else {
			script.nextToken();
		}

		behaviour->conditions.push_back(condition.release());
	}

	return true;
}

bool NpcBehavior::loadActions(ScriptReader& script, NpcBehaviour* behaviour)
{
	while (script.canRead()) {
		std::unique_ptr<NpcBehaviourAction> action(new NpcBehaviourAction);
		NpcBehaviourParameterSearch_t searchType = BEHAVIOUR_PARAMETER_NONE;

		if (script.getToken() == TOKEN_STRING) {
			action->type = BEHAVIOUR_TYPE_STRING;
			action->string = script.getString();
		} else if (script.getToken() == TOKEN_IDENTIFIER) {
			std::string identifier = script.getIdentifier();
			if (identifier == "idle") {
				action->type = BEHAVIOUR_TYPE_IDLE;
			} else if (identifier == "nop") {
				action->type = BEHAVIOUR_TYPE_NOP;
			} else if (identifier == "queue") {
				action->type = BEHAVIOUR_TYPE_QUEUE;
			} else if (identifier == "createmoney") {
				action->type = BEHAVIOUR_TYPE_CREATEMONEY;
			} else if (identifier == "deletemoney") {
				action->type = BEHAVIOUR_TYPE_DELETEMONEY;
			} else if (identifier == "promote") {
				action->type = BEHAVIOUR_TYPE_PROMOTE;
			} else if (identifier == "topic") {
				action->type = BEHAVIOUR_TYPE_TOPIC;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "price") {
				action->type = BEHAVIOUR_TYPE_PRICE;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "amount") {
				action->type = BEHAVIOUR_TYPE_AMOUNT;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "data") {
				action->type = BEHAVIOUR_TYPE_DATA;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "type") {
				action->type = BEHAVIOUR_TYPE_ITEM;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "string") {
				action->type = BEHAVIOUR_TYPE_TEXT;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "hp") {
				action->type = BEHAVIOUR_TYPE_HEALTH;
				searchType = BEHAVIOUR_PARAMETER_ASSIGN;
			} else if (identifier == "withdraw") {
				action->type = BEHAVIOUR_TYPE_WITHDRAW;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "deposit") {
				action->type = BEHAVIOUR_TYPE_DEPOSIT;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "bless") {
				action->type = BEHAVIOUR_TYPE_BLESS;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "effectme") {
				action->type = BEHAVIOUR_TYPE_EFFECTME;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "effectopp") {
				action->type = BEHAVIOUR_TYPE_EFFECTOPP;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "create") {
				action->type = BEHAVIOUR_TYPE_CREATE;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "delete") {
				action->type = BEHAVIOUR_TYPE_DELETE;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "teachspell") {
				action->type = BEHAVIOUR_TYPE_TEACHSPELL;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "town") {
				action->type = BEHAVIOUR_TYPE_TOWN;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "profession") {
				action->type = BEHAVIOUR_TYPE_PROFESSION;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "experience") {
				action->type = BEHAVIOUR_TYPE_EXPERIENCE;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "summon") {
				action->type = BEHAVIOUR_TYPE_SUMMON;
				searchType = BEHAVIOUR_PARAMETER_ONE;
			} else if (identifier == "burning") {
				action->type = BEHAVIOUR_TYPE_BURNING;
				searchType = BEHAVIOUR_PARAMETER_TWO;
			} else if (identifier == "setquestvalue") {
				action->type = BEHAVIOUR_TYPE_QUESTVALUE;
				searchType = BEHAVIOUR_PARAMETER_TWO;
			} else if (identifier == "poison") {
				action->type = BEHAVIOUR_TYPE_POISON;
				searchType = BEHAVIOUR_PARAMETER_TWO;
			} else if (identifier == "teleport") {
				action->type = BEHAVIOUR_TYPE_TELEPORT;
				searchType = BEHAVIOUR_PARAMETER_THREE;
			} else if (identifier == "createcontainer") {
				action->type = BEHAVIOUR_TYPE_CREATECONTAINER;
				searchType = BEHAVIOUR_PARAMETER_THREE;
			} else {
				script.error("illegal action term");
				return false;
			}
		} else if (script.getToken() == TOKEN_SPECIAL) {
			if (script.getSpecial() == '*') {
				if (previousBehaviour == nullptr) {
					script.error("no previous pattern");
					return false;
				}

				for (NpcBehaviourAction* actionCopy : previousBehaviour->actions) {
					behaviour->actions.push_back(actionCopy->clone());
				}
				script.nextToken();
				return true;
			}
		}

		if (searchType == BEHAVIOUR_PARAMETER_ASSIGN) {
			script.readSymbol('=');
			script.nextToken();
			NpcBehaviourNode* headNode = readValue(script);
			NpcBehaviourNode* nextNode = readFactor(script, headNode);
			action->expression = nextNode;
		} else if (searchType == BEHAVIOUR_PARAMETER_ONE) {
			script.readSymbol('(');
			script.nextToken();
			NpcBehaviourNode* headNode = readValue(script);
			NpcBehaviourNode* nextNode = readFactor(script, headNode);
			action->expression = nextNode;
			if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ')') {
				script.error("')' expected");
				return false;
			}
			script.nextToken();
		} else if (searchType == BEHAVIOUR_PARAMETER_TWO) {
			script.readSymbol('(');
			script.nextToken();
			NpcBehaviourNode* headNode = readValue(script);
			NpcBehaviourNode* nextNode = readFactor(script, headNode);
			action->expression = nextNode;
			if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ',') {
				script.error("',' expected");
				return false;
			}
			script.nextToken();
			headNode = readValue(script);
			nextNode = readFactor(script, headNode);
			action->expression2 = nextNode;
			if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ')') {
				script.error("')' expected");
				return false;
			}
			script.nextToken();
		} else if (searchType == BEHAVIOUR_PARAMETER_THREE) {
			script.readSymbol('(');
			script.nextToken();
			NpcBehaviourNode* headNode = readValue(script);
			NpcBehaviourNode* nextNode = readFactor(script, headNode);
			action->expression = nextNode;
			if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ',') {
				script.error("',' expected");
				return false;
			}
			script.nextToken();
			headNode = readValue(script);
			nextNode = readFactor(script, headNode);
			action->expression2 = nextNode;
			if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ',') {
				script.error("',' expected");
				return false;
			}
			script.nextToken();
			headNode = readValue(script);
			nextNode = readFactor(script, headNode);
			action->expression3 = nextNode;
			if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ')') {
				script.error("')' expected");
				return false;
			}
			script.nextToken();
		} else {
			script.nextToken();
		}

		behaviour->actions.push_back(action.release());

		if (script.getToken() == TOKEN_SPECIAL) {
			if (script.getSpecial() == ',') {
				script.nextToken();
				continue;
			}
		}

		break;
	}

	return true;
}

NpcBehaviourNode* NpcBehavior::readValue(ScriptReader& script)
{
	if (script.getToken() == TOKEN_NUMBER) {
		NpcBehaviourNode* node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_NUMBER;
		node->number = script.getNumber();
		script.nextToken();
		return node;
	}

	if (script.getToken() == TOKEN_STRING) {
		NpcBehaviourNode* node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_STRING;
		node->string = asLowerCaseString(script.getString());
		script.nextToken();
		return node;
	}

	if (script.getToken() == TOKEN_SPECIAL) {
		if (script.getSpecial() != '%') {
			script.error("illegal character");
			return nullptr;
		}

		NpcBehaviourNode* node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_MESSAGE_COUNT;
		node->number = script.readNumber();
		script.nextToken();
		return node;
	}

	NpcBehaviourNode* node = nullptr;
	NpcBehaviourParameterSearch_t searchType = BEHAVIOUR_PARAMETER_NONE;

	std::string identifier = script.getIdentifier();
	if (identifier == "topic") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_TOPIC;
	} else if (identifier == "price") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_PRICE;
	} else if (identifier == "type") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_ITEM;
	} else if (identifier == "string") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_TEXT;
	} else if (identifier == "data") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_DATA;
	} else if (identifier == "amount") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_AMOUNT;
	} else if (identifier == "countmoney") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_COUNTMONEY;
	} else if (identifier == "hp") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_HEALTH;
	} else if (identifier == "burning") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_BURNING;
	} else if (identifier == "level") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_LEVEL;
	} else if (identifier == "magiclevel") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_MAGICLEVEL;
	} else if (identifier == "poison") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_POISON;
	} else if (identifier == "balance") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_BALANCE;
	} else if (identifier == "spellknown") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_SPELLKNOWN;
		searchType = BEHAVIOUR_PARAMETER_ONE;
	} else if (identifier == "spelllevel") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_SPELLLEVEL;
		searchType = BEHAVIOUR_PARAMETER_ONE;
	} else if (identifier == "spellmagiclevel") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_SPELLMAGICLEVEL;
		searchType = BEHAVIOUR_PARAMETER_ONE;
	} else if (identifier == "questvalue") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_QUESTVALUE;
		searchType = BEHAVIOUR_PARAMETER_ONE;
	} else if (identifier == "count") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_COUNT;
		searchType = BEHAVIOUR_PARAMETER_ONE;
	} else if (identifier == "random") {
		node = new NpcBehaviourNode();
		node->type = BEHAVIOUR_TYPE_RANDOM;
		searchType = BEHAVIOUR_PARAMETER_TWO;
	}

	if (searchType == BEHAVIOUR_PARAMETER_ONE) {
		script.readSymbol('(');
		script.nextToken();
		NpcBehaviourNode* nextNode = readValue(script);
		nextNode = readFactor(script, nextNode);
		node->left = nextNode;
		if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ')') {
			script.error("')' expected");
		}
	} else if (searchType == BEHAVIOUR_PARAMETER_TWO) {
		script.readSymbol('(');
		script.nextToken();
		NpcBehaviourNode* nextNode = readValue(script);
		nextNode = readFactor(script, nextNode);
		node->left = nextNode;
		if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ',') {
			script.error("',' expected");
		}
		script.nextToken();
		nextNode = readValue(script);
		nextNode = readFactor(script, nextNode);
		node->right = nextNode;
		if (script.getToken() != TOKEN_SPECIAL || script.getSpecial() != ')') {
			script.error("')' expected");
		}
	}

	if (!node) {
		script.error("unknown value");
	}

	script.nextToken();
	return node;
}

NpcBehaviourNode* NpcBehavior::readFactor(ScriptReader& script, NpcBehaviourNode* nextNode)
{
	// * operator
	while (script.canRead()) {
		if (script.getToken() != TOKEN_SPECIAL) {
			break;
		}

		if (script.getSpecial() != '*') {
			break;
		}

		NpcBehaviourNode* headNode = new NpcBehaviourNode();
		headNode->type = BEHAVIOUR_TYPE_OPERATION;
		headNode->number = BEHAVIOUR_OPERATOR_MULTIPLY;
		headNode->left = nextNode;

		script.nextToken();
		nextNode = readValue(script);

		headNode->right = nextNode;
		nextNode = headNode;
	}

	// + - operators
	while (script.canRead()) {
		if (script.getToken() != TOKEN_SPECIAL) {
			break;
		}

		if (script.getSpecial() != '+' && script.getSpecial() != '-') {
			break;
		}

		NpcBehaviourNode* headNode = new NpcBehaviourNode();
		headNode->type = BEHAVIOUR_TYPE_OPERATION;
		headNode->number = BEHAVIOUR_OPERATOR_SUM;
		if (script.getSpecial() == '-') {
			headNode->number = BEHAVIOUR_OPERATOR_RES;
		}

		headNode->left = nextNode;
		script.nextToken();
		nextNode = readValue(script);

		headNode->right = nextNode;
		nextNode = headNode;
	}

	return nextNode;
}

void NpcBehavior::react(NpcBehaviourSituation_t situation, Player* player, const std::string& message)
{
	// This player has been locked out through a VANISH situation
	// meaning that this player has left the interaction with the NPC
	if (player->getID() == npc->lockVanishCreatureId) {
		return;
	}

	if ((size_t)OTSYS_TIME() < npc->reactionLockTime) {
		return;
	}

	if (situation == SITUATION_ADDRESS && !Position::areInRange<3, 3>(player->getPosition(), npc->getPosition())) {
		idle();
		return;
	}

	for (NpcBehaviour* behaviour : behaviourEntries) {
		bool fulfilled = true;

		if (situation == SITUATION_ADDRESS && behaviour->situation != SITUATION_ADDRESS) {
			continue;
		}

		if (situation == SITUATION_BUSY && behaviour->situation != SITUATION_BUSY) {
			continue;
		}

		if (situation == SITUATION_VANISH && behaviour->situation != SITUATION_VANISH) {
			continue;
		}

		if (situation == SITUATION_NONE && behaviour->situation != SITUATION_NONE) {
			continue;
		}

		std::string messageCopy = message;
		for (const NpcBehaviourCondition* condition : behaviour->conditions) {
			if (!checkCondition(condition, player, messageCopy)) {
				fulfilled = false;
				break;
			}
		}

		if (!fulfilled) {
			continue;
		}

		if (player->getID() == static_cast<uint32_t>(npc->focusCreature)) {
			topic = 0;
		}

		reset();

		if (situation == SITUATION_ADDRESS || static_cast<uint32_t>(npc->focusCreature) == player->getID()) {
			attendCustomer(player->getID());
		}

		messageCopy = message;
		for (const NpcBehaviourAction* action : behaviour->actions) {
			checkAction(action, player, messageCopy);
		}

		if (situation == SITUATION_VANISH) {
			idle();
			startToDo = true;
		}

		if (startToDo) {
			npc->startToDo();
			startToDo = false;
		}

		break;
	}
}

bool NpcBehavior::checkCondition(const NpcBehaviourCondition* condition, Player* player, std::string& message)
{
	switch (condition->type) {
		case BEHAVIOUR_TYPE_NOP: break;
		case BEHAVIOUR_TYPE_MESSAGE_COUNT: {
			int32_t value = searchDigit(message);
			if (value < condition->number) {
				return false;
			}
			break;
		}
		case BEHAVIOUR_TYPE_STRING:
			if (!searchWord(condition->string, message)) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_SORCERER:
			if (player->getVocationId() != 1 && player->getVocationId() != 5) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_DRUID:
			if (player->getVocationId() != 2 && player->getVocationId() != 6) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_PALADIN:
			if (player->getVocationId() != 3 && player->getVocationId() != 7) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_KNIGHT:
			if (player->getVocationId() != 4 && player->getVocationId() != 8) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_ISPREMIUM:
			if (!player->isPremium()) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_PVPENFORCED:
			if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_FEMALE:
			if (player->getSex() != PLAYERSEX_FEMALE) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_MALE:
			if (player->getSex() != PLAYERSEX_MALE) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_PZLOCKED:
			if (!player->isPzLocked()) {
				return false;
			}
			break;
		case BEHAVIOUR_TYPE_PROMOTED: {
			int32_t value = 0;
			player->getStorageValue(30018, value);
			if (value != 1) {
				return false;
			}
			break;
		}
		case BEHAVIOUR_TYPE_OPERATION:
			return checkOperation(player, condition->expression, message) > 0;
		case BEHAVIOUR_TYPE_SPELLKNOWN: {
			if (!player->hasLearnedInstantSpell(string)) {
				return false;
			}

			break;
		}
		default:
			std::cout << "[Warning - NpcBehavior::react]: Unhandled node type " << condition->type << std::endl;
			return false;
	}

	return true;
}

void NpcBehavior::checkAction(const NpcBehaviourAction* action, Player* player, std::string& message)
{
	switch (action->type) {
		case BEHAVIOUR_TYPE_NOP: break;
		case BEHAVIOUR_TYPE_STRING: {
			npc->addWaitToDo(talkDelay);
			npc->addActionToDo(std::bind(&Npc::doSay, npc, parseResponse(player, action->string)));
			talkDelay += 100 * (message.length() / 5) + 10000;
			startToDo = true;
			break;
		}
		case BEHAVIOUR_TYPE_IDLE:
			idle();
			break;
		case BEHAVIOUR_TYPE_QUEUE:
			queueCustomer(player->getID(), message);
			break;
		case BEHAVIOUR_TYPE_TOPIC:
			topic = evaluate(action->expression, player, message);
			break;
		case BEHAVIOUR_TYPE_PRICE:
			price = evaluate(action->expression, player, message);
			break;
		case BEHAVIOUR_TYPE_DATA:
			data = evaluate(action->expression, player, message);
			break;
		case BEHAVIOUR_TYPE_ITEM:
			type = evaluate(action->expression, player, message);
			break;
		case BEHAVIOUR_TYPE_AMOUNT:
			amount = evaluate(action->expression, player, message);
			break;
		case BEHAVIOUR_TYPE_TEXT:
			string = action->expression->string;
			break;
		case BEHAVIOUR_TYPE_HEALTH: {
			int32_t newHealth = evaluate(action->expression, player, message);
			player->changeHealth(-player->getHealth() + newHealth);
			break;
		}
		case BEHAVIOUR_TYPE_CREATEMONEY:
			if (npc->halloweenCoins) {
				do {
					int32_t count = std::min<int32_t>(100, price);
					price -= count;

					Item* item = Item::CreateItem(ITEM_HALLOWEEN_COIN, count);
					if (!item) {
						break;
					}

					ReturnValue ret = g_game.internalPlayerAddItem(player, item);
					if (ret != RETURNVALUE_NOERROR) {
						delete item;
						break;
					}
				} while (price);

				g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} CREATEMONEY (HALLOWEENCOINS) ({:d})", npc->getName(), player->getName(), price));
				break;
			}

			g_game.addMoney(player, price);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} CREATEMONEY ({:d})", npc->getName(), player->getName(), price));
			break;
		case BEHAVIOUR_TYPE_DELETEMONEY:
			if (npc->halloweenCoins) {
				player->removeItemOfType(ITEM_HALLOWEEN_COIN, price, -1);
				g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} DELETEMONEY (HALLOWEEN) ({:d})", npc->getName(), player->getName(), price));
				break;
			}

			g_game.removeMoney(player, price);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} DELETEMONEY ({:d})", npc->getName(), player->getName(), price));
			break;
		case BEHAVIOUR_TYPE_CREATE: {
			int32_t itemId = evaluate(action->expression, player, message);
			const ItemType& it = Item::items[itemId];

			if (it.stackable) {
				do {
					int32_t count = std::min<int32_t>(100, amount);
					amount -= count;

					Item* item = Item::CreateItem(itemId, count);
					if (!item) {
						break;
					}

					ReturnValue ret = g_game.internalPlayerAddItem(player, item);
					if (ret != RETURNVALUE_NOERROR) {
						delete item;
						break;
					}
				} while (amount);
			} else {
				if (it.charges) {
					data = it.charges;
				}

				if (it.isFluidContainer()) {
					data = std::max<int32_t>(0, data);
				}

				for (int32_t i = 0; i < std::max<int32_t>(1, amount); i++) {
					Item* item = Item::CreateItem(itemId, data);
					if (!item) {
						break;
					}

					ReturnValue ret = g_game.internalPlayerAddItem(player, item);
					if (ret != RETURNVALUE_NOERROR) {
						delete item;
						break;
					}
				}
			}

      g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} CREATE ({:s} - {:d})", npc->getName(), player->getName(), it.name, amount));
			break;
		}
		case BEHAVIOUR_TYPE_DELETE: {
			type = evaluate(action->expression, player, message);
			const ItemType& itemType = Item::items[type];
			if (itemType.stackable || !itemType.hasSubType()) {
				data = -1;
			}

			amount = std::max<int32_t>(1, amount);
			
			uint32_t totalRemoved = player->removeItemOfType(type, amount, data, true);
			if (totalRemoved != amount) { 
				// not all required items were removed, now remove from equipment
				player->removeItemOfType(type, amount - totalRemoved, data, false);
			}

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} DELETE ({:s} - {:d})", npc->getName(), player->name, itemType.name, amount));
			break;
		}
		case BEHAVIOUR_TYPE_EFFECTME:
			g_game.addMagicEffect(npc->getPosition(), evaluate(action->expression, player, message));
			break;
		case BEHAVIOUR_TYPE_EFFECTOPP:
			g_game.addMagicEffect(player->getPosition(), evaluate(action->expression, player, message));
			break;
		case BEHAVIOUR_TYPE_BURNING: {
			const int32_t cycles = evaluate(action->expression, player, message);
			const int32_t count = evaluate(action->expression2, player, message);

			if (count == 0) {
				player->removeCondition(CONDITION_FIRE);
				break;
			}

			ConditionDamage* conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_FIRE);
			conditionDamage->setParam(CONDITION_PARAM_CYCLE, cycles);
			conditionDamage->setParam(CONDITION_PARAM_COUNT, count);
			conditionDamage->setParam(CONDITION_PARAM_MAX_COUNT, count);
			player->addCondition(conditionDamage);
			break;
		}
		case BEHAVIOUR_TYPE_POISON: {
			const int32_t cycles = evaluate(action->expression, player, message);
			const int32_t count = evaluate(action->expression2, player, message);

			if (cycles == 0) {
				player->removeCondition(CONDITION_POISON);
				break;
			}

			ConditionDamage* conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_POISON);
			conditionDamage->setParam(CONDITION_PARAM_CYCLE, cycles);
			conditionDamage->setParam(CONDITION_PARAM_COUNT, count);
			conditionDamage->setParam(CONDITION_PARAM_MAX_COUNT, count);
			player->addCondition(conditionDamage);
			break;
		}
		case BEHAVIOUR_TYPE_TOWN:
			player->setTown(g_game.map.towns.getTown(evaluate(action->expression, player, message)));
			break;
		case BEHAVIOUR_TYPE_TEACHSPELL:
			player->learnInstantSpell(string);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} TEACHSPELL ({:s})", npc->getName(), player->getName(), string));
			break;
		case BEHAVIOUR_TYPE_QUESTVALUE: {
			int32_t questNumber = evaluate(action->expression, player, message);
			int32_t questValue = evaluate(action->expression2, player, message);
			player->addStorageValue(questNumber, questValue);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} QUESTVALUE ({:d} - {:d})", npc->getName(), player->getName(), questNumber, questValue));
			break;
		}
		case BEHAVIOUR_TYPE_TELEPORT: {
			Position pos;
			pos.x = evaluate(action->expression, player, message);
			pos.y = evaluate(action->expression2, player, message);
			pos.z = evaluate(action->expression3, player, message);
			g_game.internalTeleport(player, pos);
			break;
		}
		case BEHAVIOUR_TYPE_PROFESSION: {
			int32_t newVocation = evaluate(action->expression, player, message);
			player->setVocation(newVocation);
			break;
		}
		case BEHAVIOUR_TYPE_PROMOTE: {
			int32_t newVocation = player->getVocationId() + 4;
			player->setVocation(newVocation);
			player->addStorageValue(30018, 1);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} PROMOTE", npc->getName(), player->getName()));
			break;
		}
		case BEHAVIOUR_TYPE_SUMMON: {
			std::string name = action->expression->string;

			Monster* monster = Monster::createMonster(name);
			if (!monster) {
				break;
			}

			if (!g_game.placeCreature(monster, npc->getPosition())) {
				delete monster;
			} else {
				g_game.addMagicEffect(monster->getPosition(), CONST_ME_TELEPORT);
			}

			break;
		}
		case BEHAVIOUR_TYPE_EXPERIENCE: {
			int32_t experience = evaluate(action->expression, player, message);
			player->onGainExperience(experience, nullptr);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} EXPERIENCE ({:d})", npc->getName(), player->getName(), experience));
			break;
		}
		case BEHAVIOUR_TYPE_WITHDRAW: {
			int32_t money = evaluate(action->expression, player, message);
			player->setBankBalance(player->getBankBalance() - money);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} WITHDRAW ({:d})", npc->getName(), player->getName(), money));
			break;
		}
		case BEHAVIOUR_TYPE_DEPOSIT: {
			int32_t money = evaluate(action->expression, player, message);
			player->setBankBalance(player->getBankBalance() + money);

			g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} DEPOSIT ({:d})", npc->getName(), player->getName(), money));
			break;
		}
		case BEHAVIOUR_TYPE_BLESS: {
			uint8_t number = static_cast<uint8_t>(evaluate(action->expression, player, message)) - 1;

			if (!player->hasBlessing(number)) {
				player->addBlessing(number);
				g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} BLESS ({:d})", npc->getName(), player->getName(), static_cast<uint16_t>(number)));
			}
			break;
		}
		case BEHAVIOUR_TYPE_CREATECONTAINER: {
			int32_t containerId = evaluate(action->expression, player, message);
			int32_t itemId = evaluate(action->expression2, player, message);
			int32_t data = evaluate(action->expression3, player, message);

			if (Item::items.getItemType(itemId).isFluidContainer()) {
				data = std::max<int32_t>(0, data);
			}

			for (int32_t i = 0; i < std::max<int32_t>(1, amount); i++) {
				Item* container = Item::CreateItem(containerId);
				if (!container) {
					std::cout << "[Error - NpcBehavior::checkAction]: CreateContainer - failed to create container item" << std::endl;
					break;
				}

				Container* realContainer = container->getContainer();
				for (int32_t c = 0; c < std::max<int32_t>(1, realContainer->capacity()); c++) {
					Item* item = Item::CreateItem(itemId, data);
					if (!item) {
						std::cout << "[Error - NpcBehavior::checkAction]: CreateContainer - failed to create item" << std::endl;
						break;
					}

					realContainer->internalAddThing(item);
				}

				ReturnValue ret = g_game.internalPlayerAddItem(player, container);
				if (ret != RETURNVALUE_NOERROR) {
					delete container;
					break;
				}
			}

      g_logger.npcLog(spdlog::level::info, fmt::format("{:s} -> {:s} CREATECONTAINER ({:d},{:d})", npc->getName(), player->getName(), containerId, itemId));
			break;
		}
		default:
			std::cout << "[Warning - NpcBehavior::checkAction]: Unhandled node type " << action->type << std::endl;
			break;
	}
}

int32_t NpcBehavior::evaluate(NpcBehaviourNode* node, Player* player, std::string& message)
{
	switch (node->type) {
		case BEHAVIOUR_TYPE_NUMBER:
			return node->number;
		case BEHAVIOUR_TYPE_TOPIC:
			return topic;
		case BEHAVIOUR_TYPE_PRICE:
			return price;
		case BEHAVIOUR_TYPE_DATA:
			return data;
		case BEHAVIOUR_TYPE_ITEM:
			return type;
		case BEHAVIOUR_TYPE_AMOUNT:
			return amount;
		case BEHAVIOUR_TYPE_HEALTH:
			return player->getHealth();
		case BEHAVIOUR_TYPE_COUNT: {
			int32_t itemId = evaluate(node->left, player, message);
			const ItemType& itemType = Item::items[itemId];
			if (itemType.stackable || !itemType.hasSubType()) {
				data = -1;
			}
			return player->getItemTypeCount(itemId, data);
		}
		case BEHAVIOUR_TYPE_COUNTMONEY:
			if (npc->halloweenCoins) {
				return player->getItemTypeCount(ITEM_HALLOWEEN_COIN);
			}
			return player->getMoney();
		case BEHAVIOUR_TYPE_BURNING: {
			Condition* condition = player->getCondition(CONDITION_FIRE);
			if (!condition) {
				return false;
			}

			ConditionDamage* damage = static_cast<ConditionDamage*>(condition);
			if (damage == nullptr) {
				return false;
			}

			if (damage->getParam(CONDITION_PARAM_COUNT) > 0) {
				return damage->getParam(CONDITION_PARAM_CYCLE);
			}

			return damage->getTotalDamage();
		}
		case BEHAVIOUR_TYPE_POISON: {
			Condition* condition = player->getCondition(CONDITION_POISON);
			if (!condition) {
				return false;
			}

			ConditionDamage* damage = static_cast<ConditionDamage*>(condition);
			if (damage == nullptr) {
				return false;
			}

			if (damage->getParam(CONDITION_PARAM_COUNT) > 0) {
				return damage->getParam(CONDITION_PARAM_CYCLE);
			}

			return damage->getTotalDamage();
		}
		case BEHAVIOUR_TYPE_LEVEL:
			return player->getLevel();
		case BEHAVIOUR_TYPE_MAGICLEVEL:
			return player->getMagicLevel();
		case BEHAVIOUR_TYPE_RANDOM: {
			int32_t min = evaluate(node->left, player, message);
			int32_t max = evaluate(node->right, player, message);
			return random(min, max);
		}
		case BEHAVIOUR_TYPE_QUESTVALUE: {
			int32_t questNumber = evaluate(node->left, player, message);
			int32_t questValue;
			player->getStorageValue(questNumber, questValue);

			// CipSoft only handles storage values / quest values as 0 for "non started"
			if (questValue == -1) {
				questValue = 0;
			}

			return questValue;
		}
		case BEHAVIOUR_TYPE_MESSAGE_COUNT: {
			int32_t value = searchDigit(message);
			if (value < node->number) {
				return false;
			}
			return value;
		}
		case BEHAVIOUR_TYPE_OPERATION:
			return checkOperation(player, node, message);
		case BEHAVIOUR_TYPE_BALANCE:
			return player->getBankBalance();
		case BEHAVIOUR_TYPE_SPELLKNOWN: {
			if (player->hasLearnedInstantSpell(string)) {
				return true;
			}

			break;
		}
		case BEHAVIOUR_TYPE_SPELLLEVEL: {
			InstantSpell* spell = g_spells->getInstantSpellByName(string);
			if (!spell) {
				std::cout << "[Warning - NpcBehavior::evaluate]: SpellLevel unknown spell " << node->string << std::endl;
				return std::numeric_limits<int32_t>::max();
			}

			return spell->getLevel();
		}
		case BEHAVIOUR_TYPE_SPELLMAGICLEVEL: {
			InstantSpell* spell = g_spells->getInstantSpellByName(string);
			if (!spell) {
				std::cout << "[Warning - NpcBehavior::evaluate]: SpellLevel unknown spell " << node->string << std::endl;
				return std::numeric_limits<int32_t>::max();
			}

			return spell->getMagicLevel();
		}
		default:
			std::cout << "[Warning - NpcBehavior::evaluate]: Unhandled node type " << node->type << std::endl;
			break;
	}

	return false;
}

int32_t NpcBehavior::checkOperation(Player* player, NpcBehaviourNode* node, std::string& message)
{
	int32_t leftResult = evaluate(node->left, player, message);
	int32_t rightResult = evaluate(node->right, player, message);
	switch (node->number) {
		case BEHAVIOUR_OPERATOR_LESSER_THAN:
			return leftResult < rightResult;
		case BEHAVIOUR_OPERATOR_EQUALS:
			return leftResult == rightResult;
		case BEHAVIOUR_OPERATOR_GREATER_THAN:
			return leftResult > rightResult;
		case BEHAVIOUR_OPERATOR_GREATER_OR_EQUALS:
			return leftResult >= rightResult;
		case BEHAVIOUR_OPERATOR_LESSER_OR_EQUALS:
			return leftResult <= rightResult;
		case BEHAVIOUR_OPERATOR_NOT_EQUALS:
			return leftResult != rightResult;
		case BEHAVIOUR_OPERATOR_MULTIPLY:
			return leftResult * rightResult;
		case BEHAVIOUR_OPERATOR_SUM:
			return leftResult + rightResult;
		case BEHAVIOUR_OPERATOR_RES:
			return leftResult - rightResult;
		default:
			break;
	}
	return false;
}

int32_t NpcBehavior::searchDigit(std::string& message)
{
	int32_t start = -1;
	int32_t end = -1;
	int32_t value = 0;
	int32_t i = -1;

	for (char c : message) {
		i++;
		if (start == -1 && isdigit(c)) {
			start = i;
		} else if (start != -1 && !isdigit(c)) {
			end = i;
			break;
		}
	}

	try {
		value = std::stoi(message.substr(start, end).c_str());
	} catch (std::invalid_argument) {
		return 0;
	} catch (std::out_of_range) {
		return 0;
	}

	if (value > 500) {
		value = 500;
	}

	message = message.substr(start, message.length());
	return value;
}

bool NpcBehavior::searchWord(const std::string& pattern, std::string& message)
{
	if (pattern.empty() || message.empty()) {
		return false;
	}

	size_t len = pattern.length();
	bool wholeWord = false;

	if (pattern[len - 1] == '$') {
		len--;
		wholeWord = true;
	}

	std::string newPattern = pattern.substr(0, len);
	std::string actualMessage = asLowerCaseString(message);

	const size_t patternStart = actualMessage.find(newPattern);
	if (patternStart == std::string::npos) {
		return false;
	}

	if (patternStart > 0 && !isspace(actualMessage[patternStart - 1])) {
		return false;
	}

	if (wholeWord) {
		size_t wordPos = actualMessage.find(newPattern);
		size_t wordEnd = wordPos + newPattern.length() - 1;

		if (wordEnd + 1 > actualMessage.length()) {
			return false;
		}

		if (static_cast<int32_t>(wordPos - 1) >= 0 && !isspace(actualMessage[wordPos - 1])) {
			return false;
		}

		if (wordEnd + 1 == actualMessage.length()) {
			message = message.substr(wordEnd, message.length());
			return true;
		}

		if (!isspace(actualMessage[wordEnd + 1])) {
			return false;
		}
	}

	message = message.substr(patternStart + newPattern.length(), message.length());
	return true;
}

std::string NpcBehavior::parseResponse(Player* player, const std::string& message)
{
	std::string response = message;
	replaceString(response, "%A", std::to_string(amount));
	replaceString(response, "%D", std::to_string(data));
	replaceString(response, "%N", player->getName());
	replaceString(response, "%P", std::to_string(price));

	int32_t worldTime = g_game.getWorldTime();
	int32_t hours = std::floor<int32_t>(worldTime / 60);
	int32_t minutes = worldTime % 60;

	std::stringstream ss;
	ss << hours << ":";
	if (minutes < 10) {
		ss << '0' << minutes;
	} else {
		ss << minutes;
	}

	replaceString(response, "%T", ss.str());
	return response;
}

void NpcBehavior::attendCustomer(uint32_t playerId)
{
	std::lock_guard<std::recursive_mutex> lock(mutex);

	reset();
	npc->behaviorConversationTimeout = OTSYS_TIME() + 60000;
	npc->focusCreature = playerId;
	npc->lockVanishCreatureId = 0;
}

void NpcBehavior::queueCustomer(uint32_t playerId, const std::string& message)
{
	std::lock_guard<std::recursive_mutex> lock(mutex);

	for (NpcQueueEntry entry : queueList) {
		if (entry.playerId == playerId) {
			return;
		}
	}

	NpcQueueEntry customer;
	customer.playerId = playerId;
	customer.text = message;
	queueList.push_back(customer);
}

void NpcBehavior::idle()
{
	std::lock_guard<std::recursive_mutex> lock(mutex);

	if (queueList.empty()) {
		npc->focusCreature = 0;
		npc->lockVanishCreatureId = 0;
	} else {
		while (!queueList.empty()) {
			NpcQueueEntry nextCustomer = queueList.front();
			queueList.pop_front();
			Player* player = g_game.getPlayerByID(nextCustomer.playerId);
			if (!player) {
				continue;
			} else {
				if (!Position::areInRange<3, 3>(player->getPosition(), npc->getPosition())) {
					continue;
				}
        		npc->addWaitToDo(4000);
				npc->addActionToDo(std::bind(&NpcBehavior::react, this, SITUATION_ADDRESS, player, nextCustomer.text));
				npc->reactionLockTime = OTSYS_TIME() + 4000;
				return;
			}
		}

		npc->focusCreature = 0;
		npc->lockVanishCreatureId = 0;
	}
}

void NpcBehavior::reset()
{
	talkDelay = 1000;
}

bool NpcBehaviourCondition::setCondition(NpcBehaviourType_t _type, int32_t _number, const std::string& _string)
{
	type = _type;
	number = _number;
	string = _string;
	return false;
}
