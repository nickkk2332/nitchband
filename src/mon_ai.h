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

/* Describe a decision (options, odds and score terms) for the inspector. */
extern void mon_ai_doc(mon_ai_decision_ptr d, doc_ptr doc);

#endif
