/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "Area.hpp"
#include "Char.hpp"
#include "Descriptor.hpp"
#include "DescriptorList.hpp"
#include "MemFile.hpp"
#include "Room.hpp"
#include "db.h"
#include "handler.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "MockMud.hpp"

namespace {

test::MockMud mock_mud{};
DescriptorList descriptors{};
Logger logger{descriptors};

}

TEST_CASE("extract_char detaches the character from its descriptor both ways") {
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    test::MemFile area_file(R"(ignored~
Test~
{ 1 50} Builder A test area~
100 199
)");
    auto area = Area::parse(1, area_file.file(), "test.are", mock_mud);
    Room room{};
    room.area = &area;
    Char player{mock_mud};
    player.name = "Lyra";
    player.level = 10;
    player.pcdata = std::make_unique<PcData>();
    player.in_room = &room;
    room.people.add_back(&player);
    Descriptor desc{0, mock_mud};
    player.desc = &desc;
    desc.character(&player);

    SECTION("leaving the world (quit, purge)") {
        extract_char(&player, true);

        CHECK(desc.character() == nullptr);
        // The character stays in char_list until collect_all_garbage() at the end of the pulse, after the closed
        // descriptor has been freed: it must not still point at it, or char_update() reads freed memory (#2).
        CHECK(player.desc == nullptr);
        CHECK(std::ranges::find(reapable_chars, &player) != reapable_chars.end());
        CHECK(player.extracted);
    }
    SECTION("staying in the world (a player's death) keeps the connection") {
        extract_char(&player, false);

        CHECK(player.desc == &desc);
        CHECK(desc.character() == &player);
        CHECK_FALSE(player.extracted);
    }
    // Don't leave a pointer to this stack Char for a later collect_all_garbage().
    std::erase(reapable_chars, &player);
    if (player.in_room)
        player.in_room->people.remove(&player);
    player.in_room = nullptr;
}

TEST_CASE("run_at runs a command elsewhere and comes back") {
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    test::MemFile area_file(R"(ignored~
Test~
{ 1 50} Builder A test area~
100 199
)");
    auto area = Area::parse(1, area_file.file(), "test.are", mock_mud);
    Room home{};
    home.area = &area;
    Room there{};
    there.area = &area;
    Room elsewhere{};
    elsewhere.area = &area;
    Char imm{mock_mud};
    imm.name = "Lyra";
    imm.level = 100;
    imm.pcdata = std::make_unique<PcData>();
    imm.in_room = &home;
    home.people.add_back(&imm);

    SECTION("the command runs in the other room, then the char is back") {
        Room *ran_in = nullptr;
        run_at(&imm, &there, [&] { ran_in = imm.in_room; });

        CHECK(ran_in == &there);
        CHECK(imm.in_room == &home);
        CHECK(home.people.size() == 1);
        CHECK(there.people.empty());
    }
    SECTION("a command that moves the char still brings it back") {
        run_at(&imm, &there, [&] {
            char_from_room(&imm);
            char_to_room(&imm, &elsewhere, MoveCause::Walk);
        });

        CHECK(imm.in_room == &home);
        CHECK(elsewhere.people.empty());
    }
    SECTION("a command that takes the char out of the world (at <room> quit) leaves it out") {
        run_at(&imm, &there, [&] { extract_char(&imm, true); });

        // Before, do_at found the char still in char_list (it's only freed at the end of the pulse) and put it back
        // in home, which then pointed at freed memory: an immortal's "at <room> quit" crashed the mud.
        CHECK(imm.in_room == nullptr);
        CHECK(home.people.empty());
        CHECK(there.people.empty());
    }
    std::erase(reapable_chars, &imm);
    if (imm.in_room)
        imm.in_room->people.remove(&imm);
    imm.in_room = nullptr;
}
