/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Bot/Engine/AiObjectContext.h"
#include "Bot/Extension/PlayerbotExtension.h"
#include "Bot/PlayerbotAI.h"
#include "Bot/Recovery/PlayerbotObjectiveQuarantine.h"
#include "Bot/Recovery/PlayerbotRecovery.h"
#include "Bot/Recovery/PlayerbotRecoveryAction.h"
#include "Engine.h"
#include "IntegrationTestFixture.h"
#include "Player.h"
#include "SharedDefines.h"
#include "gtest/gtest.h"

void AddPlayerbotsRecoveryScripts();

TEST(PlayerbotRecoveryIntegrationTest, NullBotCannotMutateWorldState)
{
    EXPECT_EQ(PlayerbotRecoverToHomebind(nullptr), PlayerbotHomebindRecoveryResult::NotInWorld);
    EXPECT_EQ(PlayerbotRecoveryObjectiveProgress(nullptr, PlayerbotLoopObjectiveKind::Quest, 1), 0U);
}

class PlayerbotRecoveryFingerprintTest : public IntegrationTestFixture
{
};

TEST_F(PlayerbotRecoveryFingerprintTest, ModuleMakesRecoveryStrategiesAndTriggersReachable)
{
    AddPlayerbotsRecoveryScripts();
    AiObjectContext::BuildAllSharedContexts();

    TestPlayer* const bot = CreateTestPlayer();
    PlayerbotAI botAI(bot);
    Engine engine(&botAI, botAI.GetAiObjectContext());
    GetPlayerbotExtensionRegistry().ForEach(
        [bot, &botAI, &engine](PlayerbotExtension& extension)
        {
            extension.AddDefaultCombatStrategies(bot, &botAI, engine);
            extension.AddDefaultNonCombatStrategies(bot, &botAI, engine);
        });

    ASSERT_TRUE(engine.HasStrategy("playerbots recovery"));
    ASSERT_TRUE(engine.HasStrategy("playerbots combat recovery"));
    ::Action* const action = botAI.GetAiObjectContext()->GetAction("recover stuck movement");
    EXPECT_NE(dynamic_cast<PlayerbotRecoveryAction*>(action), nullptr);
    EXPECT_NE(botAI.GetAiObjectContext()->GetTrigger("playerbots movement stuck"), nullptr);
    EXPECT_NE(botAI.GetAiObjectContext()->GetTrigger("playerbots combat group stuck"), nullptr);
}

TEST_F(PlayerbotRecoveryFingerprintTest, QuestFingerprintChangesOnlyAfterAuthoritativeProgress)
{
    TestPlayer* const bot = CreateTestPlayer();
    PlayerbotAI botAI(bot);
    std::uint64_t const questKey = PlayerbotObjectiveQuarantine::QuestKey(91, 0);
    QuestStatusData& questStatus = bot->getQuestStatusMap()[91];
    questStatus.Status = QUEST_STATUS_INCOMPLETE;

    std::uint64_t const initial =
        PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Quest, questKey);
    EXPECT_EQ(initial, PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Quest, questKey));

    questStatus.CreatureOrGOCount[0] = 1;
    EXPECT_NE(initial, PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Quest, questKey));
}

TEST_F(PlayerbotRecoveryFingerprintTest, ExperienceAndSkillProgressChangeTheirOwnFingerprints)
{
    TestPlayer* const bot = CreateTestPlayer();
    PlayerbotAI botAI(bot);

    std::uint64_t const initialGrind = PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Grind, 0);
    bot->SetUInt32Value(PLAYER_XP, 10);
    EXPECT_NE(initialGrind, PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Grind, 0));

    SkillStatusMap& skills = bot->GetSkillStatusMap();
    skills.emplace(SKILL_BLACKSMITHING, SkillStatusData(0, SKILL_NEW));
    bot->SetUInt32Value(PLAYER_SKILL_INDEX(0), MAKE_PAIR32(SKILL_BLACKSMITHING, 1));
    bot->SetUInt32Value(PLAYER_SKILL_VALUE_INDEX(0), MAKE_SKILL_VALUE(1, 75));
    std::uint64_t const initialProfession =
        PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Profession, 0);
    bot->SetUInt32Value(PLAYER_SKILL_VALUE_INDEX(0), MAKE_SKILL_VALUE(2, 75));
    EXPECT_NE(initialProfession, PlayerbotRecoveryObjectiveProgress(&botAI, PlayerbotLoopObjectiveKind::Profession, 0));
}
