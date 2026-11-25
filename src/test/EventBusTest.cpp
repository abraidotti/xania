/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#include "EventBus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

struct Arrived {
    static constexpr std::string_view name = "Arrived";
    int who;
};

struct Left {
    static constexpr std::string_view name = "Left";
    int who;
};

struct BeforeHit {
    static constexpr std::string_view name = "BeforeHit";
    int amount;
    bool cancelled = false;
};

static_assert(Event<Arrived>);
static_assert(!BeforeEvent<Arrived>);
static_assert(BeforeEvent<BeforeHit>);
static_assert(!Event<int>);
// A notification's listeners can't change it.
static_assert(std::is_same_v<EventBus::Listener<Arrived>, std::function<void(const Arrived &)>>);
static_assert(std::is_same_v<EventBus::Listener<BeforeHit>, std::function<void(BeforeHit &)>>);

}

TEST_CASE("event bus notifications") {
    EventBus bus;
    std::vector<std::string> calls;

    SECTION("an event with no listeners does nothing") {
        bus.emit(Arrived{1});
        CHECK(bus.listener_count<Arrived>() == 0);
    }
    SECTION("listeners get the event") {
        auto sub = bus.on<Arrived>("a", [&](const Arrived &e) { calls.push_back("a" + std::to_string(e.who)); });
        bus.emit(Arrived{7});
        CHECK(calls == std::vector<std::string>{"a7"});
    }
    SECTION("only listeners for that type of event") {
        auto arrived = bus.on<Arrived>("arrived", [&](const Arrived &) { calls.push_back("arrived"); });
        auto left = bus.on<Left>("left", [&](const Left &) { calls.push_back("left"); });
        bus.emit(Left{1});
        CHECK(calls == std::vector<std::string>{"left"});
    }
    SECTION("lowest priority number first, then in the order they subscribed") {
        auto late = bus.on<Arrived>(
            "late", [&](const Arrived &) { calls.push_back("late"); }, 10);
        auto first = bus.on<Arrived>("first", [&](const Arrived &) { calls.push_back("first"); });
        auto early = bus.on<Arrived>(
            "early", [&](const Arrived &) { calls.push_back("early"); }, -10);
        auto second = bus.on<Arrived>("second", [&](const Arrived &) { calls.push_back("second"); });
        bus.emit(Arrived{1});
        CHECK(calls == std::vector<std::string>{"early", "first", "second", "late"});
    }
}

TEST_CASE("event bus before events") {
    EventBus bus;
    std::vector<std::string> calls;

    SECTION("listeners can change the event, and the caller sees it") {
        auto halve = bus.on<BeforeHit>("halve", [](BeforeHit &e) { e.amount /= 2; });
        auto add = bus.on<BeforeHit>(
            "add one", [](BeforeHit &e) { e.amount += 1; }, 1);
        BeforeHit hit{10};
        CHECK(bus.emit(hit));
        CHECK(hit.amount == 6);
    }
    SECTION("the action goes ahead if no listener cancels it") {
        BeforeHit hit{10};
        CHECK(bus.emit(hit));
        CHECK(hit.amount == 10);
    }
    SECTION("a listener can cancel it, and later listeners don't run") {
        auto first = bus.on<BeforeHit>("first", [&](BeforeHit &) { calls.push_back("first"); });
        auto block = bus.on<BeforeHit>("block", [&](BeforeHit &e) {
            calls.push_back("block");
            e.cancelled = true;
        });
        auto after = bus.on<BeforeHit>("after", [&](BeforeHit &) { calls.push_back("after"); });
        BeforeHit hit{10};
        CHECK_FALSE(bus.emit(hit));
        CHECK(calls == std::vector<std::string>{"first", "block"});
    }
}

TEST_CASE("event bus subscriptions") {
    EventBus bus;
    int count = 0;

    SECTION("a destroyed subscription unsubscribes") {
        {
            auto sub = bus.on<Arrived>("count", [&](const Arrived &) { ++count; });
            CHECK(bus.listener_count<Arrived>() == 1);
            bus.emit(Arrived{1});
        }
        CHECK(bus.listener_count<Arrived>() == 0);
        bus.emit(Arrived{1});
        CHECK(count == 1);
    }
    SECTION("reset() unsubscribes, once") {
        auto sub = bus.on<Arrived>("count", [&](const Arrived &) { ++count; });
        CHECK(sub.active());
        sub.reset();
        CHECK_FALSE(sub.active());
        sub.reset();
        bus.emit(Arrived{1});
        CHECK(count == 0);
    }
    SECTION("a moved subscription keeps the listener subscribed") {
        EventBus::Subscription kept;
        {
            auto sub = bus.on<Arrived>("count", [&](const Arrived &) { ++count; });
            kept = std::move(sub);
        }
        CHECK(kept.active());
        bus.emit(Arrived{1});
        CHECK(count == 1);
    }
    SECTION("moving onto a subscription unsubscribes what it held") {
        auto a = bus.on<Arrived>("a", [&](const Arrived &) { count += 1; });
        auto b = bus.on<Arrived>("b", [&](const Arrived &) { count += 10; });
        a = std::move(b);
        bus.emit(Arrived{1});
        CHECK(count == 10);
        CHECK(bus.listener_count<Arrived>() == 1);
    }
    SECTION("a subscription can outlive the bus") {
        EventBus::Subscription sub;
        {
            EventBus short_lived;
            sub = short_lived.on<Arrived>("count", [&](const Arrived &) { ++count; });
        }
        CHECK_FALSE(sub.active());
        sub.reset();
    }
}

TEST_CASE("event bus changes during a dispatch") {
    EventBus bus;
    std::vector<std::string> calls;

    SECTION("a listener can unsubscribe itself") {
        EventBus::Subscription self;
        self = bus.on<Arrived>("once", [&](const Arrived &) {
            calls.push_back("once");
            self.reset();
        });
        bus.emit(Arrived{1});
        bus.emit(Arrived{1});
        CHECK(calls == std::vector<std::string>{"once"});
    }
    SECTION("a listener unsubscribed by an earlier one doesn't run") {
        EventBus::Subscription second;
        auto first = bus.on<Arrived>("first", [&](const Arrived &) {
            calls.push_back("first");
            second.reset();
        });
        second = bus.on<Arrived>("second", [&](const Arrived &) { calls.push_back("second"); });
        bus.emit(Arrived{1});
        CHECK(calls == std::vector<std::string>{"first"});
    }
    SECTION("a listener subscribed during a dispatch first runs on the next one") {
        EventBus::Subscription added;
        auto adder = bus.on<Arrived>("adder", [&](const Arrived &) {
            calls.push_back("adder");
            if (!added.active())
                added = bus.on<Arrived>("added", [&](const Arrived &) { calls.push_back("added"); });
        });
        bus.emit(Arrived{1});
        CHECK(calls == std::vector<std::string>{"adder"});
        bus.emit(Arrived{1});
        CHECK(calls == std::vector<std::string>{"adder", "adder", "added"});
    }
}

TEST_CASE("event bus nesting") {
    SECTION("listeners can emit other events, which run straight away") {
        EventBus bus;
        std::vector<std::string> calls;
        auto on_left = bus.on<Left>("left", [&](const Left &) { calls.push_back("left"); });
        auto on_arrived = bus.on<Arrived>("arrived", [&](const Arrived &) {
            calls.push_back("arrived");
            bus.emit(Left{1});
            calls.push_back("arrived done");
        });
        bus.emit(Arrived{1});
        CHECK(calls == std::vector<std::string>{"arrived", "left", "arrived done"});
        CHECK(bus.depth() == 0);
    }
    SECTION("a loop of events stops at the depth limit") {
        EventBus bus(3);
        int arrivals = 0;
        int deepest = 0;
        auto loop = bus.on<Arrived>("loop", [&](const Arrived &e) {
            ++arrivals;
            deepest = std::max(deepest, bus.depth());
            bus.emit(Arrived{e.who + 1});
        });
        bus.emit(Arrived{1});
        CHECK(arrivals == 3);
        CHECK(deepest == 3);
        CHECK(bus.dropped() == 1);
        CHECK(bus.depth() == 0);
    }
    SECTION("a before event dropped at the depth limit goes ahead unchanged") {
        EventBus bus(1);
        BeforeHit inner{10};
        bool inner_went_ahead = false;
        auto block = bus.on<BeforeHit>("block", [](BeforeHit &e) { e.cancelled = true; });
        auto arrived = bus.on<Arrived>("arrived", [&](const Arrived &) { inner_went_ahead = bus.emit(inner); });
        bus.emit(Arrived{1});
        CHECK(inner_went_ahead);
        CHECK(inner.amount == 10);
        CHECK(bus.dropped() == 1);
    }
}

TEST_CASE("event bus lists its listeners") {
    EventBus bus;
    auto b = bus.on<Left>("left logger", [](const Left &) {});
    auto a2 = bus.on<Arrived>(
        "greeter", [](const Arrived &) {}, 5);
    auto a1 = bus.on<Arrived>(
        "tracker", [](const Arrived &) {}, -5);
    using Info = EventBus::ListenerInfo;
    CHECK(bus.listeners()
          == std::vector<Info>{Info{"Arrived", "tracker", -5}, Info{"Arrived", "greeter", 5},
                               Info{"Left", "left logger", 0}});
}
