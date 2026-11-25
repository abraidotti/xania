/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "DeathCounter.hpp"
#include "Area.hpp"
#include "Char.hpp"
#include "CharActFlag.hpp"
#include "DescriptorList.hpp"
#include "Events.hpp"
#include "Logging.hpp"
#include "MemFile.hpp"
#include "Room.hpp"
#include "common/BitOps.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include "MockMud.hpp"

namespace {

test::MockMud mock_mud{};
DescriptorList descriptors{};
Logger logger{descriptors};

Area make_area(int num, const char *name) {
    test::MemFile file(
        fmt::format("ignored~\n{}~\n{{ 1 50}} Builder A test area~\n{} {}\n", name, num * 100, num * 100 + 99));
    return Area::parse(num, file.file(), "test.are", mock_mud);
}

}

TEST_CASE("death counter") {
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    auto arachnos = make_area(1, "Arachnos");
    auto moria = make_area(2, "Moria");
    Room in_arachnos{};
    in_arachnos.area = &arachnos;
    Room in_moria{};
    in_moria.area = &moria;
    Char mob{mock_mud};
    set_enum_bit(mob.act, CharActFlag::Npc);
    Char player{mock_mud};
    player.pcdata = std::make_unique<PcData>();

    EventBus bus;
    DeathCounter counter{bus};
    auto die_in = [&](Room &room, Char &victim, bool duel = false) {
        victim.in_room = &room;
        bus.emit(Events::CharKilled{&player, &victim, duel});
        victim.in_room = nullptr;
    };

    SECTION("nobody yet") {
        CHECK(counter.by_area().empty());
        CHECK(counter.report() == std::vector<std::string>{"Nobody has died since the mud booted."});
    }
    SECTION("players and mobs counted apart, by the room they died in, most deaths first") {
        die_in(in_arachnos, mob);
        die_in(in_moria, mob);
        die_in(in_moria, player);
        die_in(in_moria, mob);
        const auto counts = counter.by_area();
        REQUIRE(counts.size() == 2);
        CHECK(counts[0].area == &moria);
        CHECK(counts[0].players == 1);
        CHECK(counts[0].mobs == 2);
        CHECK(counts[1].area == &arachnos);
        CHECK(counts[1].mobs == 1);
        CHECK(counter.report()
              == std::vector<std::string>{"Area                           Players    Mobs",
                                          "Moria                                1       2",
                                          "Arachnos                             0       1"});
    }
    SECTION("ties in area order") {
        die_in(in_moria, mob);
        die_in(in_arachnos, mob);
        const auto counts = counter.by_area();
        REQUIRE(counts.size() == 2);
        CHECK(counts[0].area == &arachnos);
    }
    SECTION("losing a duel isn't dying") {
        die_in(in_moria, player, true);
        CHECK(counter.by_area().empty());
    }
}
