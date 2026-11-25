/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2026 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#pragma once

#include "MProgTriggerCtx.hpp"

#include <string_view>

struct Char;
struct Object;
struct Room;

// Why char_to_room() put a char in a room. Every caller must say, so that what "arriving" means is decided in one
// place: the listeners of Events::CharEnteredRoom.
enum class MoveCause {
    Walk, // move_char: walking, riding, following
    Portal, // do_enter
    Recall, // the recall command and word of recall
    Magic, // gate, summon, teleport, a tornado
    Transfer, // an immortal's goto or transfer, a mob program's mpgoto or mptransfer
    Visit, // there for a moment and back: at, mpat, teleport object
    Created, // a new mob placed in the world: resets, mload, mpmload, clone, a bought pet, the executioner's guards
    Login, // entering the game, or the player file upgrader loading a char
    Limbo, // an idle player (and pet) going to Limbo, or coming back
    Death, // a dead player sent to the altar
    Special // a mob's special procedure moving it (Death, Phil)
};

// The events the engine emits on Mud::events(). Each says where it's emitted and what's true then.
namespace Events {

// char_to_room() has put ch in room (light and area occupancy updated). Notification only: the walking messages, the
// look and the followers haven't happened yet for a walk; see CharArrived.
struct CharEnteredRoom {
    static constexpr std::string_view name = "CharEnteredRoom";
    Char *ch;
    Room *room;
    MoveCause cause;
};

// A walk (move_char) or a step through a portal (do_enter) has finished: ch has arrived, looked, and its followers have
// followed (each with their own CharArrived first). Mob programs' entry and greet triggers run here.
struct CharArrived {
    static constexpr std::string_view name = "CharArrived";
    Char *ch;
};

// act() sent message to `to` from a call that allows mob triggers (MobTrig::Yes). Mob programs' act trigger queues it.
struct ActSeen {
    static constexpr std::string_view name = "ActSeen";
    std::string_view message;
    Char *to;
    const Char *ch;
    const Object *obj;
    MProg::Target target;
};

// ch said message out loud (Char::say), after everyone in the room was told. Mob programs' speech trigger.
struct CharSpoke {
    static constexpr std::string_view name = "CharSpoke";
    const Char *ch;
    std::string_view message;
};

// giver gave obj to receiver (Char::give), after the messages. Mob programs' give trigger.
struct ObjectGiven {
    static constexpr std::string_view name = "ObjectGiven";
    Char *giver;
    Char *receiver;
    Object *obj;
};

// giver gave amount gold to receiver (do_give), after the messages. Mob programs' bribe trigger.
struct GoldGiven {
    static constexpr std::string_view name = "GoldGiven";
    Char *giver;
    Char *receiver;
    int amount;
};

// violence_update: ch had its round of combat and is still fighting victim. Mob programs' hitprcnt and fight
// triggers, in that order.
struct CombatRound {
    static constexpr std::string_view name = "CombatRound";
    Char *ch;
    Char *victim;
};

// raw_kill: victim has been killed by killer (who may be victim itself, or another char; never null). Emitted first,
// while victim is still in the room where it died: before its death cry, corpse and extraction. In a duel, the loser
// "dies" but keeps everything and stays where they are. Mob programs' death trigger.
struct CharKilled {
    static constexpr std::string_view name = "CharKilled";
    Char *killer;
    Char *victim;
    bool duel;
};

// mobile_update: the turn of a mob that isn't charmed, asleep or busy, in an area with players in it, after its special
// procedure and before it scavenges or wanders. Mob programs' random trigger.
struct MobUpdate {
    static constexpr std::string_view name = "MobUpdate";
    Char *mob;
};

}
