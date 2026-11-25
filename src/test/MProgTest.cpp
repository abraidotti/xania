/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "MProg.hpp"
#include "CharActFlag.hpp"
#include "Descriptor.hpp"
#include "DescriptorList.hpp"
#include "Interpreter.hpp"
#include "Logging.hpp"
#include "MProgTypeFlag.hpp"
#include "MemFile.hpp"
#include "MobIndexData.hpp"
#include "Object.hpp"
#include "ObjectIndex.hpp"
#include "Room.hpp"
#include "Socials.hpp"
#include "common/BitOps.hpp"
#include "db.h"
#include "handler.hpp"
#include "update.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "MockMud.hpp"

namespace {

test::MockMud mock_mud{};
DescriptorList descriptors{};
Logger logger{descriptors};
Interpreter interpreter{};
Socials socials{};

constexpr auto social_db = R"(
smile
You smile happily.
$n smiles happily.
You smile at $M.
$n beams a smile at $N.
$n smiles at you.
There's no one by that name around.
You smile at yourself.
$n smiles at $r.
#

#0
)";

MobIndexData load_orc_index() {
    test::MemFile mob_file(R"mob(
#13000
orc~
the orc shaman~
~
~
orc~
0 0 0 0
0 0 1d6+1 1d6+1 1d1+1 blast
0 0 0 0
0 0 0 0
stand stand male 200
0 0 medium 0

#0
)mob");
    return *MobIndexData::from_file(mob_file.file(), logger);
}

Char &add_mob(MobIndexData &mob_idx, std::string name, Room &room) {
    auto &mob = *char_list.emplace_back(std::make_unique<Char>(mock_mud));
    mob.name = name;
    mob.short_descr = std::move(name);
    mob.mobIndex = &mob_idx;
    set_enum_bit(mob.act, CharActFlag::Npc);
    mob.in_room = &room;
    room.people.add_back(&mob);
    return mob;
}

}

TEST_CASE("run_queued_acts") {
    test::MemFile social_file(social_db);
    socials.load(social_file.file(), logger);
    ALLOW_CALL(mock_mud, descriptors()).LR_RETURN(descriptors);
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    ALLOW_CALL(mock_mud, interpreter()).LR_RETURN(interpreter);
    ALLOW_CALL(mock_mud, socials()).LR_RETURN(socials);
    test::MemFile mob_file(R"mob(
#13000
orc~
the orc shaman~
~
~
orc~
0 0 0 0
0 0 1d6+1 1d6+1 1d1+1 blast
0 0 0 0
0 0 0 0
stand stand male 200
0 0 medium 0

#0
)mob");
    auto mob_idx = *MobIndexData::from_file(mob_file.file(), logger);
    // Whenever it sees someone smile, it smiles at itself: an act it sees too, so the program queues another act
    // while the queue runs.
    mob_idx.mobprogs.push_back(MProg::Program{MProg::TypeFlag::Act, "smiles", {"smile self"}});
    set_enum_bit(mob_idx.progtypes, MProg::TypeFlag::Act);

    Room room{};
    Char orc{mock_mud};
    orc.name = "orc";
    orc.short_descr = "the orc shaman";
    orc.mobIndex = &mob_idx;
    set_enum_bit(orc.act, CharActFlag::Npc);
    orc.in_room = &room;
    room.people.add_back(&orc);
    Char bob{mock_mud};
    bob.name = "Bob";
    bob.pcdata = std::make_unique<PcData>();
    bob.in_room = &room;
    room.people.add_back(&bob);
    Descriptor bob_desc{0, mock_mud};
    bob.desc = &bob_desc;
    bob_desc.character(&bob);

    MProg::act_trigger("Bob smiles happily.", &orc, &bob, nullptr, nullptr);
    MProg::act_trigger("Bob smiles happily.", &orc, &bob, nullptr, nullptr);
    // A full queue, so an append while it runs reallocates it. Before, the loop then read the second message from
    // freed memory.
    orc.mpact.shrink_to_fit();
    REQUIRE(orc.mpact.size() == 2);

    MProg::run_queued_acts(&orc);

    constexpr auto two_smiles = "\n\rThe orc shaman smiles at themself.\n\rThe orc shaman smiles at themself.\n\r";
    CHECK(bob_desc.buffered_output() == two_smiles);
    // The orc's own smiles, seen while the queue ran, are dropped rather than run next pulse.
    CHECK(orc.mpact.empty());
    MProg::run_queued_acts(&orc);
    CHECK(bob_desc.buffered_output() == two_smiles);

    room.people.remove(&orc);
    room.people.remove(&bob);
    bob.desc = nullptr;
    bob_desc.character(nullptr);
}

TEST_CASE("queued acts about a char or object leaving the world are dropped") {
    test::MemFile social_file(social_db);
    socials.load(social_file.file(), logger);
    ALLOW_CALL(mock_mud, descriptors()).LR_RETURN(descriptors);
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    ALLOW_CALL(mock_mud, interpreter()).LR_RETURN(interpreter);
    ALLOW_CALL(mock_mud, socials()).LR_RETURN(socials);
    auto orc_idx = load_orc_index();
    // Its program names whoever it saw: $n reads the queued message's char.
    orc_idx.mobprogs.push_back(MProg::Program{MProg::TypeFlag::Act, "smiles", {"smile $n"}});
    set_enum_bit(orc_idx.progtypes, MProg::TypeFlag::Act);
    auto goblin_idx = load_orc_index();
    Room room{};
    auto &orc = add_mob(orc_idx, "orc", room);
    auto *goblin = &add_mob(goblin_idx, "goblin", room);

    SECTION("a char") {
        MProg::act_trigger("The goblin smiles happily.", &orc, goblin, nullptr, nullptr);
        MProg::act_trigger("The wind howls.", &orc, nullptr, nullptr, nullptr);
        MProg::act_trigger("Something looks at the goblin.", &orc, nullptr, nullptr, goblin);
        extract_char(goblin, true);
        collect_all_garbage(); // frees the goblin

        // Only the message that isn't about the goblin is left. Before, the first one stayed, and running it read the
        // freed goblin for $n.
        CHECK(orc.mpact.size() == 1);
        CHECK(orc.mpact[0].act_message_trigger() == "The wind howls.");
        MProg::run_queued_acts(&orc);
    }
    SECTION("an object") {
        ObjectIndex obj_index{};
        Object dagger{&obj_index, logger};
        MProg::act_trigger("The goblin drops a dagger.", &orc, goblin, &dagger, nullptr);
        MProg::act_trigger("The goblin looks at a dagger.", &orc, goblin, nullptr, &dagger);
        MProg::act_trigger("The goblin smiles happily.", &orc, goblin, nullptr, nullptr);
        extract_obj(&dagger);
        std::erase(reapable_objects, &dagger); // it's on the stack: don't let collect_all_garbage() see it

        REQUIRE(orc.mpact.size() == 1);
        CHECK(orc.mpact[0].act_message_trigger() == "The goblin smiles happily.");
        orc.mpact.clear();
    }
    for (auto *person : room.people)
        room.people.remove(person);
    std::erase_if(reapable_chars, [](auto *) { return true; });
    std::erase_if(char_list, [&](const auto &uch) { return uch.get() == &orc || uch.get() == goblin; });
}
