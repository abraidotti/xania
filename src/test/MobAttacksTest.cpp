#include "MobAttacks.hpp"
#include "AffectFlag.hpp"
#include "BodyPartFlag.hpp"
#include "BodySize.hpp"
#include "Char.hpp"
#include "CharActFlag.hpp"
#include "OffensiveFlag.hpp"
#include "SkillTables.hpp"
#include "common/BitOps.hpp"
#include "lookup.h"

#include <catch2/catch_test_macros.hpp>

#include "MockMud.hpp"

namespace {

test::MockMud mock_mud{};

void make_mob(Char &ch) {
    set_enum_bit(ch.act, CharActFlag::Npc);
    ch.level = 10;
    ch.body_size = BodySize::Medium;
    ch.position = Position::Type::Fighting;
    for (auto stat : {Stat::Str, Stat::Int, Stat::Wis, Stat::Dex, Stat::Con})
        ch.perm_stat[stat] = 13;
}

}

TEST_CASE("mob tail and crush attacks") {
    Char attacker{mock_mud};
    Char victim{mock_mud};
    make_mob(attacker);
    make_mob(victim);

    SECTION("has a tail only with the tail body part") {
        CHECK(!MobAttacks::has_tail(attacker));
        set_enum_bit(attacker.parts, BodyPartFlag::Tail);
        CHECK(MobAttacks::has_tail(attacker));
    }

    SECTION("crush") {
        SECTION("works on a victim up to one size bigger") {
            victim.body_size = BodySize::Large;
            CHECK(MobAttacks::can_crush(attacker, victim));
            victim.body_size = BodySize::Tiny;
            CHECK(MobAttacks::can_crush(attacker, victim));
        }
        SECTION("doesn't work on a victim two sizes bigger") {
            victim.body_size = BodySize::Huge;
            CHECK(!MobAttacks::can_crush(attacker, victim));
        }
        SECTION("chance between equals: skill + str - dex * 4/3") {
            // 30 + 13 - 17
            CHECK(MobAttacks::crush_chance(attacker, victim, 30) == 26);
        }
        SECTION("each size bigger the attacker is adds 10, each size smaller takes 10") {
            attacker.body_size = BodySize::Large;
            CHECK(MobAttacks::crush_chance(attacker, victim, 30) == 36);
            attacker.body_size = BodySize::Small;
            CHECK(MobAttacks::crush_chance(attacker, victim, 30) == 16);
        }
        SECTION("speed and level") {
            set_enum_bit(attacker.off_flags, OffensiveFlag::Fast);
            CHECK(MobAttacks::crush_chance(attacker, victim, 30) == 36);
            set_enum_bit(victim.affected_by, AffectFlag::Haste);
            CHECK(MobAttacks::crush_chance(attacker, victim, 30) == 16);
            attacker.level = 15;
            CHECK(MobAttacks::crush_chance(attacker, victim, 30) == 26);
        }
    }

    SECTION("tail") {
        SECTION("chance between equals: skill + dex - dex * 3/2") {
            // 30 + 13 - 19
            CHECK(MobAttacks::tail_chance(attacker, victim, 30) == 24);
        }
        SECTION("a bigger victim is harder to hit, a smaller one no easier") {
            victim.body_size = BodySize::Huge;
            CHECK(MobAttacks::tail_chance(attacker, victim, 30) == 4);
            victim.body_size = BodySize::Tiny;
            CHECK(MobAttacks::tail_chance(attacker, victim, 30) == 24);
        }
        SECTION("the others caught by the sweep have half the chance") {
            CHECK(MobAttacks::sweep_chance(24) == 12);
            CHECK(MobAttacks::sweep_chance(1) == 0);
        }
        SECTION("knocks down a victim standing on the ground") { CHECK(MobAttacks::can_knock_down(attacker, victim)); }
        SECTION("doesn't knock down a flying victim") {
            set_enum_bit(victim.affected_by, AffectFlag::Flying);
            CHECK(!MobAttacks::can_knock_down(attacker, victim));
        }
        SECTION("doesn't knock down a mounted victim") {
            Char mount{mock_mud};
            victim.riding = &mount;
            CHECK(!MobAttacks::can_knock_down(attacker, victim));
        }
        SECTION("doesn't knock down a victim already down") {
            victim.position = Position::Type::Resting;
            CHECK(!MobAttacks::can_knock_down(attacker, victim));
        }
        SECTION("doesn't knock down a victim two sizes bigger") {
            victim.body_size = BodySize::Large;
            CHECK(MobAttacks::can_knock_down(attacker, victim));
            victim.body_size = BodySize::Huge;
            CHECK(!MobAttacks::can_knock_down(attacker, victim));
        }
    }

    SECTION("max damage is bash's: 2 + 2 * attacker size + chance / 20") {
        CHECK(MobAttacks::max_damage(BodySize::Tiny, 0) == 2);
        CHECK(MobAttacks::max_damage(BodySize::Medium, 40) == 8);
        CHECK(MobAttacks::max_damage(BodySize::Giant, 100) == 17);
    }
}

TEST_CASE("tail and crush skills") {
    for (const auto *name : {"tail", "crush"}) {
        const auto sn = skill_lookup(name);
        REQUIRE(sn > 0);
        const auto &skill = skill_table[sn];
        CHECK(skill.name == std::string_view(name));
        CHECK(skill.verb == std::string_view(name));
        for (const auto rating : skill.rating)
            CHECK(rating == SkillRatingUnattainable);
    }
}
