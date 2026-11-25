/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#pragma once

#include "EventBus.hpp"

#include <string>
#include <vector>

struct Area;

// Counts the deaths in each area since boot, for immortals (the "deaths" command). It's built only from
// Events::CharKilled: no code in fight.cpp knows it exists, which is the point of it (#16). Duels don't count.
class DeathCounter {
public:
    explicit DeathCounter(EventBus &bus);
    DeathCounter(const DeathCounter &) = delete;
    DeathCounter &operator=(const DeathCounter &) = delete;

    struct Count {
        const Area *area;
        int players{};
        int mobs{};
        [[nodiscard]] int total() const { return players + mobs; }
    };
    // Areas with at least one death, most deaths first; ties by area number.
    [[nodiscard]] std::vector<Count> by_area() const;
    // The command's output: one line per area, or a line saying there have been none.
    [[nodiscard]] std::vector<std::string> report() const;

private:
    std::vector<Count> counts_;
    EventBus::Subscription subscription_;
};
