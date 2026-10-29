// quest_strings.hpp - the base game's quest strings as data. importQuestStrings (import.hpp) puts each one in the
// ContentDB as "base:str/quest/<key>"; a mod replaces or adds one by using the same id.
// {name} placeholders are filled by fillTemplate (quest_text.hpp). Keep the text exactly as shown: no trimming.
#pragma once
#include <utility>

namespace gp
{
    inline const std::pair<const char *, const char *> kQuestStrings[] = {
        // GetQuestCompleteDialogue
        {"complete_2", "Oh, thanks!"},
        {"complete_3", "How nice! Thank you!"},
        {"complete_6", "Mmmmm! Thank you very much."},
        {"complete_7", "Great! The area you cleaned up looks so nice now. Thanks!"},
        {"complete_default_1", "Thank you for bringing me these items! Here is your reward."},
        {"complete_default_2", "Let's see... Yes! You have found the items I need. Thanks!"},
        {"complete_default_3", "You found everything! Here is your reward for helping me."},
        // GetQuestReminderDialogue
        {"reminder_2", "I don't feel well..."},
        {"reminder_3", "Sigh..."},
        {"reminder_6", "Oh, I thought you had my lunch..."},
        {"reminder_7", "Hmm. You haven't found all of the litter yet."},
        {"reminder_default_1", "You don't have all the items I need yet."},
        {"reminder_default_2", "Let's see... Nope. You haven't collected everything yet."},
        {"reminder_default_3", "I really need you to bring me all the items."},
        // GetQuestRequestDialogue. {list} = "2 apples, 3 pears and 1 plum", built in code.
        {"request_2", "{npc} {illness} and really needs {item}."},
        {"request_3_1", "I heard {npc} is sad and needs some cheering up."},
        {"request_3_2", "{npc} seems sad."},
        {"request_3_3", "I'd really like to try to cheer up {npc}."},
        {"request_6", "I'm so hungry. I'm looking for someone to get my lunch."},
        {"request_7", "There is a lot of litter around {area} that needs to be picked up."},
        {"request_default_1", "I would be grateful if you would help me find {list}."},
        {"request_default_2", "I really need {list}."},
        {"request_default_3", "I need someone to find me {list}."},
        // BuildNextQuestHintMessage
        {"hint_1", "I hear that {npc} may need some help. Find {npc} {prep} {area}."},
        {"hint_2", "If you want to help some more, go to {area} and find {npc}."},
        {"hint_3", "{npc} {prep} {area} was looking for some help."},
        // BuildQuestStatusText (stats screen)
        {"status_where", "Find {npc} {prep} {area}"},
        {"status_next", "{where} for your next quest."},
        {"status_complete", "{where} to complete your quest."},
        {"status_find", "Find {list}."},
        // DrawQuestObjectiveScreen: the headings (ROM 0xd140, 0xd137, 0xd146, 0xd153) and "Me" (0xd150)
        {"objective_find", "Find:"},
        {"objective_take", "Take:"},
        {"objective_bring", "Bring To:"},
        {"objective_look", "Look In:"},
        {"objective_me", "Me"},
        // Quest failure and dev placeholders
        {"quest_failed", "Quest failed! Go see {npc} {prep} {area} to try again."},
        {"quest_failed_short", "Quest failed!"},
        {"placeholder_generated", "(generated quest text)"},
        // The prompt on a generated quest's offer (Yes / No), one of three; and the answer to No. ROM 0xcacb.. and 0xcfc2.. (the
        // way each is picked is inferred: one of three at random, like the other quest texts)
        {"accept_1", "Would you please help me with this task?"},
        {"accept_2", "Can you please help me?"},
        {"accept_3", "Do you have time to help me with this?"},
        {"decline_1", "OK. If you change your mind, please come find me again."},
        {"decline_2", "That's OK. Please come back if you have time to help me later."},
        {"decline_3", "Please find me again if you feel like working later."},
        // GetAreaNamePreposition (by area tier), and the word that joins the last item of a list
        {"prep_on", "on"},
        {"prep_at", "at"},
        {"prep_in", "in"},
        {"list_and", "and"},
        // GetIllnessDescriptionText: illness_<medicine item number in hex>_<variant 1..>. More than one variant = random pick.
        {"illness_59_1", "is molting"},
        {"illness_4f_1", "has brain freeze"},
        {"illness_54_1", "has cold feet"},
        {"illness_4c_1", "has brain ache"},
        {"illness_56_1", "has the oogies"},
        {"illness_50_1", "has cooties"},
        {"illness_4e_1", "has a fever"},
        {"illness_4e_2", "has a cold"},
        {"illness_57_1", "has dog breath"},
        {"illness_58_1", "has gone bananas"},
        {"illness_default", "has panda pox"},
    };
}