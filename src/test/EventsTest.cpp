/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "Events.hpp"
#include "Area.hpp"
#include "Char.hpp"
#include "DescriptorList.hpp"
#include "EventBus.hpp"
#include "Logging.hpp"
#include "MProg.hpp"
#include "MemFile.hpp"
#include "Room.hpp"
#include "handler.hpp"

#include <catch2/catch_test_macros.hpp>

#include "MockMud.hpp"

namespace {

test::MockMud mock_mud{};
DescriptorList descriptors{};
Logger logger{descriptors};

}

TEST_CASE("char_to_room tells listeners who arrived where, and why") {
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    test::MemFile area_file(R"(ignored~
Test~
{ 1 50} Builder A test area~
100 199
)");
    auto area = Area::parse(1, area_file.file(), "test.are", mock_mud);
    Room room{};
    room.area = &area;
    Char mob{mock_mud};
    mob.name = "orc";

    std::vector<Events::CharEnteredRoom> seen;
    Room *room_when_told = nullptr;
    auto sub = mock_mud.events().on<Events::CharEnteredRoom>("test", [&](const Events::CharEnteredRoom &e) {
        seen.push_back(e);
        room_when_told = e.ch->in_room;
    });
    char_to_room(&mob, &room, MoveCause::Recall);

    REQUIRE(seen.size() == 1);
    CHECK(seen[0].ch == &mob);
    CHECK(seen[0].room == &room);
    CHECK(seen[0].cause == MoveCause::Recall);
    CHECK(room_when_told == &room); // already in the room when listeners run

    SECTION("but not when there's no room") {
        char_from_room(&mob);
        char_to_room(&mob, nullptr, MoveCause::Walk);
        CHECK(seen.size() == 1);
    }
    if (mob.in_room)
        char_from_room(&mob);
}

TEST_CASE("mob program triggers listen to their events") {
    EventBus bus;
    auto subscriptions = MProg::listen(bus);
    using Info = EventBus::ListenerInfo;
    CHECK(bus.listeners()
          == std::vector<Info>{{"ActSeen", "mob programs: act", 0},
                               {"CharArrived", "mob programs: entry, greet", 0},
                               {"CharKilled", "mob programs: death", 0},
                               {"CharSpoke", "mob programs: speech", 0},
                               {"CombatRound", "mob programs: hitprcnt, fight", 0},
                               {"GoldGiven", "mob programs: bribe", 0},
                               {"MobUpdate", "mob programs: random", 0},
                               {"ObjectGiven", "mob programs: give", 0}});
    subscriptions.clear();
    CHECK(bus.listeners().empty());
}

TEST_CASE("which arrivals greet") {
    using enum MoveCause;
    for (const auto cause : {Walk, Portal, Recall, Magic})
        CHECK(MProg::greets(cause));
    for (const auto cause : {Transfer, Visit, Created, Login, Limbo, Death, Special})
        CHECK_FALSE(MProg::greets(cause));
}
