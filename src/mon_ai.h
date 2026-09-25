#ifndef INCLUDED_MON_AI_H
#define INCLUDED_MON_AI_H

/* Monster turn decisions.
 *
 * Each turn a monster weighs a small set of candidate actions ("options"),
 * each with a score built up from named terms so the wizard AI inspector can
 * show exactly why a monster did what it did. One option is then picked at
 * random in proportion to its score.
 *
 * The option set currently mirrors the classic rule ("roll the race spell
 * frequency; on success try a spell, otherwise move or melee"), and the pick
 * consumes random numbers exactly as that rule did, so behaviour is
 * unchanged. Later work adds options (reposition, retreat, ...) here rather
 * than as special cases inside process_monster(). */

enum {
    MAI_CAST = 0,    /* try a spell; the spell AI in monspell.c chooses which */
    MAI_STEP_AWAY,   /* frail caster backs off from the player (one step) */
    MAI_HOLD,        /* frail caster at range keeps its distance (waits) */
    MAI_RETREAT,     /* shaken and hurt: break off and regroup */
    MAI_FLANK,       /* squad flanker: go around to the player's far side */
    MAI_WAIT,        /* squad member that can't reach the player: wait, don't jostle */
    MAI_PHYSICAL,    /* everything else: move, melee, breed, pick up, ... */
    MAI_KIND_MAX
};

#define MAI_MAX_OPTIONS 8
#define MAI_MAX_TERMS   10

typedef struct {
    cptr name;       /* what adjusted the score */
    int  value;      /* the score after this term */
} mon_ai_term_t;

typedef struct {
    int           kind;
    int           score;
    int           term_ct;
    mon_ai_term_t terms[MAI_MAX_TERMS];
} mon_ai_option_t, *mon_ai_option_ptr;

typedef struct {
    mon_ptr         mon;
    int             option_ct;
    mon_ai_option_t options[MAI_MAX_OPTIONS];
    int             choice;  /* index into options; -1 until chosen */
    int             step_dir; /* keypad direction for MAI_STEP_AWAY or MAI_FLANK */
} mon_ai_decision_t, *mon_ai_decision_ptr;

/* Score this turn's options. No random numbers are used. */
extern void mon_ai_decide(mon_ptr mon, bool cast_blocked, mon_ai_decision_ptr d);

/* Pick one option in proportion to score and return its kind. Rolls only
 * when there is more than one option. */
extern int  mon_ai_choose(mon_ai_decision_ptr d);

/* The score of the cast option alone: percent chance to try a spell this
 * turn, ignoring the random anti-streak block. */
extern int  mon_ai_cast_score(mon_ptr mon);

extern cptr mon_ai_kind_name(int kind);

/* Effective speed this turn (as process_monsters computes it) */
extern int  mon_ai_speed(mon_ptr mon);

/* For a frail caster next to the player: the keypad direction of a free
 * square that gets it out of melee while keeping a line of fire, or 0 if
 * it can't (or shouldn't) step away. No random numbers are used. */
extern int  mon_ai_step_away_dir(mon_ptr mon);

/* Describe a decision (options, odds and score terms) for the inspector. */
extern void mon_ai_doc(mon_ai_decision_ptr d, doc_ptr doc);

/* Perception.
 *
 * Awake hostile monsters no longer always know where the player is. Each
 * turn mon_ai_perceive() decides whether the monster has contact (adjacent,
 * line of sight, hearing within a stealth-dependent range, or scent for
 * animals). With contact it hunts as before; without, it heads for the
 * player's last known position, searches around it for a while, and then
 * idles. Pack members share what they perceive. */
enum {
    MAI_S_UNSET = 0,    /* not evaluated yet (new, just woken, or loaded) */
    MAI_S_HUNTING,      /* has contact now */
    MAI_S_TRACKING,     /* heading for the last known position */
    MAI_S_SEARCHING,    /* looking around the last known position */
    MAI_S_IDLE,         /* gave up */
    MAI_S_MAX
};
enum {
    MAI_C_NONE = 0,
    MAI_C_ADJACENT,
    MAI_C_SIGHT,
    MAI_C_HEARING,
    MAI_C_SCENT,
    MAI_C_MAX
};

extern void mon_ai_perceive(mon_ptr mon);
extern bool mon_ai_has_contact(mon_ptr mon);
extern int  mon_ai_hearing_range(mon_ptr mon);
extern cptr mon_ai_state_name(int state);
extern cptr mon_ai_contact_name(int contact);

/* Movement for a hostile monster without contact. Fills mm[] and returns
 * TRUE, or returns FALSE if the monster should stay where it is. */
extern bool mon_ai_track_moves(mon_ptr mon, int *mm);

/* The monster learns where the player is (woken up, hurt by the player) */
extern void mon_ai_alert(mon_ptr mon);

/* Intents: plans that span turns.
 * - Charge: a big spell is announced one turn before it is released; enough
 *   damage, a stun or confusion interrupts it.
 * - Regroup: a shaken, hurt monster breaks contact, recovers, then returns
 *   to where it last knew the player to be. */
enum {
    MAI_I_NONE = 0,
    MAI_I_CHARGE,
    MAI_I_REGROUP,
    MAI_I_MAX
};
extern cptr mon_ai_intent_name(int intent);

/* Start of a monster's turn: carry out an ongoing intent. TRUE if that
 * used the turn. */
extern bool mon_ai_intent_turn(mon_ptr mon);
extern void mon_ai_start_charge(mon_ptr mon, int type, int effect);
extern bool mon_ai_retreat_moves(mon_ptr mon, int *mm);

/* Morale: 100 is steady. Damage and packmates dying lower it; it recovers
 * out of contact. */
extern int  mon_ai_morale(mon_ptr mon);
extern void mon_ai_on_hurt(mon_ptr mon, int dam);
extern void mon_ai_on_disabled(mon_ptr mon);   /* stunned or confused */
extern void mon_ai_on_ally_death(mon_ptr dead);

/* Squads: packs of 3+ hunting the player (pack AI "Seek") get roles and a
 * plan worked out once per player turn from the terrain around the player.
 * - Surround (player in the open): flankers path to the free square next to
 *   the player farthest from where their packmates are already engaging.
 * - Chokepoint (player in a corridor): members that can't reach a free
 *   square next to the player wait instead of jostling.
 * A living leader steadies nearby members' morale and barks orders when
 * the squad commits to a plan. */
enum {
    MAI_R_NONE = 0,
    MAI_R_LEADER,
    MAI_R_FRONTLINE,
    MAI_R_FLANKER,
    MAI_R_ARTILLERY,
    MAI_R_SUPPORT,
    MAI_R_MAX
};
enum {
    MAI_P_NONE = 0,
    MAI_P_SURROUND,
    MAI_P_CHOKE,
    MAI_P_MAX
};
extern int  mon_ai_role(mon_ptr mon);
extern int  mon_ai_squad_plan(mon_ptr mon);
extern cptr mon_ai_role_name(int role);
extern cptr mon_ai_plan_name(int plan);

/* Player actions make noise; monsters hear a loud player from farther */
#define MAI_NOISE_MELEE   6
#define MAI_NOISE_MISSILE 4
#define MAI_NOISE_SPELL   8
#define MAI_NOISE_BASH    10
extern void mon_ai_player_noise(int loudness);

/* Behaviour counters for one monster, filled in while mon_ai_stats.m_idx
 * names it (the ^A K wizard harness). */
typedef struct {
    int m_idx;              /* monster being measured; 0 = off */
    int pack_idx;           /* ... and its whole pack, if set */
    int turns;              /* turns the monster took */
    int adjacent;           /* ... that began next to the player */
    int kinds[MAI_KIND_MAX];/* option picked (spellcasters only) */
    int spells;             /* spells cast at the player */
    int blinks;             /* blinked itself away */
    int blink_other;        /* blinked the player away */
    int tele_other;         /* teleported the player away */
    int melee;              /* melee attacks on the player */
    int states[MAI_S_MAX];  /* perception state at the start of its turns */
    int charges;            /* big spells announced */
    int releases;           /* ... and released */
    int interrupts;         /* ... and interrupted */
    int retreats;           /* turns spent retreating */
    int shouts;             /* called for help */
    int barks;              /* leader barked orders */
} mon_ai_stats_t;
extern mon_ai_stats_t mon_ai_stats;
extern bool mon_ai_tracked(mon_ptr mon);

#endif
