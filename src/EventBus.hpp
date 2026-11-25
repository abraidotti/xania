/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// A small, typed, synchronous event bus (see issue #16).
//
// An event is a plain struct with a name:
//
//   struct CharDied {
//       static constexpr std::string_view name = "CharDied";
//       Char *victim;
//       Char *killer;
//   };
//
// Code that something happened in emits it, and every listener for that type runs inside emit(), lowest priority
// number first, ties in the order they subscribed:
//
//   auto sub = bus.on<CharDied>("death counter", [](const CharDied &e) { ... });
//   bus.emit(CharDied{victim, killer});
//
// There are two kinds of event, and the type system keeps them apart:
//  - A notification (any event without a `cancelled` member) says something has happened. Its listeners get a const
//    reference, so they can react but can't change it.
//  - A "before" event has a `bool cancelled` member. Its listeners get a mutable reference, so they can change its
//    fields (reduce an amount of damage, say) or cancel the action. Once one cancels, the rest don't run. emit()
//    returns whether the action should go ahead, and the caller reads back any fields the listeners changed.
//
// Listeners may emit events themselves. To stop a chain of listeners looping forever, emit() runs no listeners once
// events are nested max_depth deep: it counts the event in dropped() and lets a "before" action go ahead.
//
// on() returns a Subscription, and the listener stays subscribed until that is destroyed or reset(). A listener may
// unsubscribe itself or others while an event is being dispatched: a removed listener doesn't run again, even later in
// the same dispatch. A listener subscribed during a dispatch is active straight away, but first runs once every
// dispatch of that event in progress has finished. Dispatch doesn't allocate.

template <typename E>
concept Event = std::is_class_v<E> && requires {
    { E::name } -> std::convertible_to<std::string_view>;
};

template <typename E>
concept BeforeEvent = Event<E> && requires(E &e) {
    { e.cancelled } -> std::same_as<bool &>;
};

class EventBus {
    struct ChannelBase;

public:
    // A notification's listeners see it as const; a "before" event's listeners may change it.
    template <Event E>
    using Listener = std::conditional_t<BeforeEvent<E>, std::function<void(E &)>, std::function<void(const E &)>>;

    // Keeps a listener subscribed. Move-only; unsubscribes when destroyed. Safe to outlive the bus.
    class Subscription {
    public:
        Subscription() = default;
        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;
        Subscription(Subscription &&other) noexcept
            : channel_(std::move(other.channel_)), id_(std::exchange(other.id_, 0)) {}
        Subscription &operator=(Subscription &&other) noexcept {
            if (this != &other) {
                reset();
                channel_ = std::move(other.channel_);
                id_ = std::exchange(other.id_, 0);
            }
            return *this;
        }
        ~Subscription() { reset(); }

        // Unsubscribes the listener now. Does nothing if it already was.
        void reset() {
            if (auto channel = channel_.lock())
                channel->remove(id_);
            channel_.reset();
            id_ = 0;
        }
        // Is the listener still subscribed?
        [[nodiscard]] bool active() const {
            const auto channel = channel_.lock();
            return channel && channel->contains(id_);
        }

    private:
        friend class EventBus;
        Subscription(std::weak_ptr<ChannelBase> channel, uint64_t id) : channel_(std::move(channel)), id_(id) {}
        std::weak_ptr<ChannelBase> channel_;
        uint64_t id_{};
    };

    // One subscribed listener, as listeners() reports it.
    struct ListenerInfo {
        std::string_view event;
        std::string name;
        int priority;
        bool operator==(const ListenerInfo &) const = default;
    };

    static constexpr int DefaultMaxDepth = 8;

    explicit EventBus(int max_depth = DefaultMaxDepth) : max_depth_(max_depth) {}
    EventBus(const EventBus &) = delete;
    EventBus &operator=(const EventBus &) = delete;

    // Subscribes a listener to events of type E. The name is what listeners() reports, for debugging.
    template <Event E>
    [[nodiscard]] Subscription on(std::string name, Listener<E> listener, int priority = 0) {
        auto &channel = channel_for<E>();
        const auto id = ++last_id_;
        channel.add(typename Channel<E>::Entry{id, priority, std::move(name), std::move(listener)});
        return Subscription{channels_.at(std::type_index(typeid(E))), id};
    }

    // Tells every listener a notification happened.
    template <Event E>
    requires(!BeforeEvent<E>) void emit(const E &event) { dispatch<E, const E>(event); }

    // Lets every listener change or cancel an action before it happens. Returns false if one cancelled it.
    template <BeforeEvent E>
    [[nodiscard]] bool emit(E &event) {
        dispatch<E, E>(event);
        return !event.cancelled;
    }

    // How many listeners events of type E have.
    template <Event E>
    [[nodiscard]] size_t listener_count() const {
        const auto it = channels_.find(std::type_index(typeid(E)));
        return it == channels_.end() ? 0 : it->second->size();
    }

    // Every subscribed listener, grouped by event name and in the order each event runs them.
    [[nodiscard]] std::vector<ListenerInfo> listeners() const {
        std::vector<ListenerInfo> result;
        for (const auto &[type, channel] : channels_)
            channel->describe(result);
        std::ranges::stable_sort(result, std::less<>{}, &ListenerInfo::event);
        return result;
    }

    // How deeply events are nested right now: 1 inside a listener, 2 inside a listener of an event that one emitted...
    [[nodiscard]] int depth() const { return depth_; }
    // How many events were emitted too deeply nested to run their listeners.
    [[nodiscard]] uint64_t dropped() const { return dropped_; }

private:
    struct ChannelBase {
        virtual ~ChannelBase() = default;
        virtual void remove(uint64_t id) = 0;
        [[nodiscard]] virtual bool contains(uint64_t id) const = 0;
        [[nodiscard]] virtual size_t size() const = 0;
        virtual void describe(std::vector<ListenerInfo> &out) const = 0;
    };

    template <Event E>
    struct Channel final : ChannelBase {
        struct Entry {
            uint64_t id;
            int priority;
            std::string name;
            Listener<E> fn;
            bool live{true};
        };
        // Sorted by priority, then by id (the order they subscribed). While a dispatch runs, entries keeps its size, so
        // dispatch can walk it in place without copying it: a new listener waits in pending, and a removed one is only
        // marked dead. settle() catches up once the outermost dispatch of this channel ends.
        std::vector<Entry> entries;
        std::vector<Entry> pending;
        int dispatching{};

        void add(Entry entry) {
            if (dispatching)
                pending.push_back(std::move(entry));
            else
                insert_sorted(std::move(entry));
        }
        void insert_sorted(Entry entry) {
            const auto pos = std::ranges::upper_bound(entries, entry.priority, std::less<>{}, &Entry::priority);
            entries.insert(pos, std::move(entry));
        }
        void settle() {
            std::erase_if(entries, [](const Entry &e) { return !e.live; });
            for (auto &entry : std::exchange(pending, {}))
                insert_sorted(std::move(entry));
        }
        void remove(uint64_t id) override {
            if (std::erase_if(pending, [id](const Entry &e) { return e.id == id; }))
                return;
            const auto it = std::ranges::find(entries, id, &Entry::id);
            if (it == entries.end())
                return;
            if (dispatching)
                it->live = false;
            else
                entries.erase(it);
        }
        [[nodiscard]] bool contains(uint64_t id) const override {
            const auto it = std::ranges::find(entries, id, &Entry::id);
            return (it != entries.end() && it->live) || std::ranges::find(pending, id, &Entry::id) != pending.end();
        }
        [[nodiscard]] size_t size() const override {
            return static_cast<size_t>(std::ranges::count_if(entries, &Entry::live)) + pending.size();
        }
        void describe(std::vector<ListenerInfo> &out) const override {
            for (const auto &e : entries)
                if (e.live)
                    out.push_back({E::name, e.name, e.priority});
            for (const auto &e : pending)
                out.push_back({E::name, e.name, e.priority});
        }
    };

    template <Event E>
    Channel<E> &channel_for() {
        auto &channel = channels_[std::type_index(typeid(E))];
        if (!channel)
            channel = std::make_shared<Channel<E>>();
        return static_cast<Channel<E> &>(*channel);
    }

    template <Event E, typename Arg>
    void dispatch(Arg &event) {
        const auto it = channels_.find(std::type_index(typeid(E)));
        if (it == channels_.end())
            return;
        auto &channel = static_cast<Channel<E> &>(*it->second);
        if (channel.entries.empty())
            return;
        if (depth_ >= max_depth_) {
            ++dropped_;
            return;
        }
        struct Guard {
            int &depth;
            Channel<E> &channel;
            Guard(int &d, Channel<E> &c) : depth(d), channel(c) {
                ++depth;
                ++channel.dispatching;
            }
            ~Guard() {
                --depth;
                if (--channel.dispatching == 0)
                    channel.settle();
            }
        } guard{depth_, channel};
        for (auto &entry : channel.entries) {
            if (!entry.live)
                continue;
            entry.fn(event);
            if constexpr (BeforeEvent<E>) {
                if (event.cancelled)
                    return;
            }
        }
    }

    std::unordered_map<std::type_index, std::shared_ptr<ChannelBase>> channels_;
    uint64_t last_id_{};
    int max_depth_;
    int depth_{};
    uint64_t dropped_{};
};
