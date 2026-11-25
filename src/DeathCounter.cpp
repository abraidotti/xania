/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "DeathCounter.hpp"
#include "Area.hpp"
#include "Char.hpp"
#include "Events.hpp"
#include "Room.hpp"

#include <algorithm>
#include <fmt/format.h>

DeathCounter::DeathCounter(EventBus &bus)
    : subscription_(bus.on<Events::CharKilled>("death counter", [this](const Events::CharKilled &e) {
          if (e.duel || !e.victim->in_room)
              return;
          const auto *area = e.victim->in_room->area;
          auto it = std::ranges::find(counts_, area, &Count::area);
          if (it == counts_.end())
              it = counts_.insert(counts_.end(), Count{area});
          ++(e.victim->is_npc() ? it->mobs : it->players);
      })) {}

std::vector<DeathCounter::Count> DeathCounter::by_area() const {
    auto result = counts_;
    std::ranges::sort(result, [](const Count &a, const Count &b) {
        if (a.total() != b.total())
            return a.total() > b.total();
        return a.area->num() < b.area->num();
    });
    return result;
}

std::vector<std::string> DeathCounter::report() const {
    const auto counts = by_area();
    if (counts.empty())
        return {"Nobody has died since the mud booted."};
    std::vector<std::string> lines{fmt::format("{:<30} {:>7} {:>7}", "Area", "Players", "Mobs")};
    for (const auto &count : counts)
        lines.push_back(fmt::format("{:<30} {:>7} {:>7}", count.area->short_name(), count.players, count.mobs));
    return lines;
}
