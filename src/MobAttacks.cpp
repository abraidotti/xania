/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "MobAttacks.hpp"
#include "BodyPartFlag.hpp"
#include "BodySize.hpp"
#include "Char.hpp"
#include "OffensiveFlag.hpp"
#include "common/BitOps.hpp"

#include <magic_enum/magic_enum.hpp>

namespace MobAttacks {

namespace {

// How much bigger the attacker is than the victim, in sizes. Negative when the victim is the bigger one.
int size_advantage(const Char &attacker, const Char &victim) {
    return BodySizes::size_diff(attacker.body_size, victim.body_size);
}

bool is_fast(const Char &ch) { return check_enum_bit(ch.off_flags, OffensiveFlag::Fast) || ch.is_aff_haste(); }

int speed_and_level(const Char &attacker, const Char &victim) {
    auto modifier = 0;
    if (is_fast(attacker))
        modifier += 10;
    if (is_fast(victim))
        modifier -= 20;
    return modifier + (attacker.level - victim.level) * 2;
}

}

bool has_tail(const Char &attacker) { return check_enum_bit(attacker.parts, BodyPartFlag::Tail); }

bool can_crush(const Char &attacker, const Char &victim) { return size_advantage(attacker, victim) >= -1; }

int crush_chance(const Char &attacker, const Char &victim, const int skill) {
    auto chance = skill;
    chance += size_advantage(attacker, victim) * 10;
    chance += attacker.curr_stat(Stat::Str);
    chance -= victim.curr_stat(Stat::Dex) * 4 / 3;
    return chance + speed_and_level(attacker, victim);
}

int tail_chance(const Char &attacker, const Char &victim, const int skill) {
    auto chance = skill;
    if (const auto advantage = size_advantage(attacker, victim); advantage < 0)
        chance += advantage * 10;
    chance += attacker.curr_stat(Stat::Dex);
    chance -= victim.curr_stat(Stat::Dex) * 3 / 2;
    return chance + speed_and_level(attacker, victim);
}

int sweep_chance(const int target_chance) { return target_chance / 2; }

bool can_knock_down(const Char &attacker, const Char &victim) {
    return !victim.is_aff_fly() && !victim.riding && !victim.is_pos_relaxing() && !victim.is_pos_stunned_or_dying()
           && size_advantage(attacker, victim) >= -1;
}

int max_damage(const BodySize attacker_size, const int chance) {
    return 2 + (2 * magic_enum::enum_integer<BodySize>(attacker_size) + (chance / 20));
}

}
