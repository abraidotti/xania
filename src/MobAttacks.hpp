/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#pragma once

enum class BodySize;
struct Char;

// The odds and damage of the mob-only special attacks that mob_hit() rolls for: the tail sweep (OffensiveFlag::Tail)
// and the crush (OffensiveFlag::Crush). They start from bash and trip: a percentage chance from the attacker's skill,
// size, stats, speed and level difference, and bash's damage.
namespace MobAttacks {

// Whether the attacker has a tail to sweep with, from its race or its own body parts.
[[nodiscard]] bool has_tail(const Char &attacker);

// A crush needs a grip on the victim: it can't be done to a victim more than one size bigger than the attacker.
[[nodiscard]] bool can_crush(const Char &attacker, const Char &victim);

// The chance (out of 100) that a crush lands. Bigger attackers and stronger ones find it easier.
[[nodiscard]] int crush_chance(const Char &attacker, const Char &victim, int skill);

// The chance (out of 100) that a tail sweep hits its target. Like trip, a bigger or more agile victim is harder to hit.
[[nodiscard]] int tail_chance(const Char &attacker, const Char &victim, int skill);

// The chance for each other character fighting the attacker to be caught by the same sweep: half the target's chance.
[[nodiscard]] int sweep_chance(int target_chance);

// Whether a tail that hits also knocks the victim down. Not if the victim is airborne, mounted, already down, or more
// than one size bigger than the attacker.
[[nodiscard]] bool can_knock_down(const Char &attacker, const Char &victim);

// The maximum damage either attack does. The same as bash: larger attackers and better odds hit harder.
[[nodiscard]] int max_damage(BodySize attacker_size, int chance);

}
