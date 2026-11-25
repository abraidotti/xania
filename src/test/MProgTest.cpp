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
#include "Room.hpp"
#include "Socials.hpp"
#include "common/BitOps.hpp"

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
