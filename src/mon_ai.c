/* Monster turn decisions: candidate actions scored by named terms.
 * See mon_ai.h. */

#include "angband.h"

#include <assert.h>

mon_ai_stats_t mon_ai_stats = {0};

static int _path_step(mon_ptr mon, int ty, int tx);
static int _greedy_step(mon_ptr mon, int ty, int tx);

bool mon_ai_tracked(mon_ptr mon)
{
    if (!mon_ai_stats.m_idx || !mon) return FALSE;
    if (mon->id == mon_ai_stats.m_idx) return TRUE;
    return mon_ai_stats.pack_idx && mon->pack_idx == mon_ai_stats.pack_idx;
}

cptr mon_ai_kind_name(int kind)
{
    switch (kind)
    {
    case MAI_CAST: return "Cast a spell";
    case MAI_STEP_AWAY: return "Step away from the player";
    case MAI_HOLD: return "Hold position at range";
    case MAI_RETREAT: return "Retreat to regroup";
    case MAI_FLANK: return "Flank the player";
    case MAI_WAIT: return "Wait for an opening";
    case MAI_LURK: return "Lie in wait";
    case MAI_GUARD: return "Keep to its post";
    case MAI_PHYSICAL: return "Move or melee";
    }
    return "?";
}

/*************************************************************************
 * Archetypes
 ************************************************************************/
static cptr _arch_names[MAI_A_MAX] = {
    "NONE", "BRUTE", "SKIRMISHER", "ARTILLERY", "SUPPORT", "AMBUSHER",
    "GUARDIAN", "TRICKSTER", "BERSERKER",
};

cptr mon_ai_archetype_name(int arch)
{
    static cptr pretty[MAI_A_MAX] = {
        "None", "Brute", "Skirmisher", "Artillery", "Support", "Ambusher",
        "Guardian", "Trickster", "Berserker",
    };
    if (arch < 0 || arch >= MAI_A_MAX) return "?";
    return pretty[arch];
}

cptr mon_ai_archetype_article(int arch)
{
    static cptr names[MAI_A_MAX] = {
        "?", "a Brute", "a Skirmisher", "Artillery", "Support", "an Ambusher",
        "a Guardian", "a Trickster", "a Berserker",
    };
    if (arch <= 0 || arch >= MAI_A_MAX) return "?";
    return names[arch];
}

int mon_ai_archetype(monster_race *r_ptr)
{
    if (r_ptr->ai_arch) return r_ptr->ai_arch;
    if (mon_race_weak_melee(r_ptr) && mon_race_has_attack_spell(r_ptr))
        return MAI_A_ARTILLERY;
    return MAI_A_BRUTE;
}

/* Does the race back off to cast rather than trade blows? */
bool mon_ai_race_kites(monster_race *r_ptr)
{
    if (!mon_race_has_attack_spell(r_ptr)) return FALSE;
    switch (mon_ai_archetype(r_ptr))
    {
    case MAI_A_ARTILLERY:
        return TRUE;
    case MAI_A_SUPPORT:
    case MAI_A_TRICKSTER:
        return mon_race_weak_melee(r_ptr);
    }
    return FALSE;
}

int mon_ai_spell_type_pct(monster_race *r_ptr, int type)
{
    if (!r_ptr->ai_arch) return 100;  /* inferred archetypes keep the classic weights */
    switch (r_ptr->ai_arch)
    {
    case MAI_A_TRICKSTER:
        if (type == MST_TACTIC || type == MST_ESCAPE || type == MST_ANNOY || type == MST_BIFF) return 200;
        break;
    case MAI_A_SUPPORT:
        if (type == MST_HEAL || type == MST_BUFF) return 200;
        break;
    case MAI_A_BERSERKER:
        if (type == MST_ESCAPE) return 0;
        break;
    }
    return 100;
}

/* Personality bits from a hash of things fixed at birth, so no random
 * numbers are drawn and a reloaded monster keeps its character */
static int _pers(mon_ptr mon)
{
    if (!mon->ai_pers)
    {
        u32b h = (u32b)mon->id * 2654435761u ^ (u32b)mon->r_idx * 40503u ^ (u32b)mon->maxhp * 97u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        mon->ai_pers = 0x80 | (h & 0x0f);
    }
    return mon->ai_pers;
}

static int _pers_axis(mon_ptr mon, int shift)
{
    int v;
    if (r_info[mon->r_idx].flags1 & RF1_UNIQUE) return 0;
    v = (_pers(mon) >> shift) & 3;
    if (v == 0) return -1;  /* 1 in 4 */
    if (v == 3) return 1;   /* 1 in 4 */
    return 0;
}
int mon_ai_courage(mon_ptr mon) { return _pers_axis(mon, 0); }
int mon_ai_temper(mon_ptr mon) { return _pers_axis(mon, 2); }

void mon_ai_describe_tactics(mon_ptr mon, doc_ptr doc)
{
    static cptr courage[3] = { "timid", "steady", "bold" };
    static cptr temper[3] = { "careful", "even-tempered", "aggressive" };
    monster_race *r_ptr = &r_info[mon->r_idx];

    doc_printf(doc, "Archetype: <color:B>%s</color>%s%s%s, %s and %s\n",
        mon_ai_archetype_name(mon_ai_archetype(r_ptr)),
        r_ptr->ai_arch ? "" : " (inferred)",
        (r_ptr->ai_traits & MAI_T_COWARDLY) ? ", cowardly" : "",
        (r_ptr->ai_traits & MAI_T_BRAVE) ? ", brave" : "",
        courage[mon_ai_courage(mon) + 1], temper[mon_ai_temper(mon) + 1]);
    if (mon_ai_archetype(r_ptr) == MAI_A_GUARDIAN && (mon->home_y || mon->home_x))
        doc_printf(doc, "Guards (%d,%d), %d squares away\n", mon->home_x, mon->home_y,
            distance(mon->fy, mon->fx, mon->home_y, mon->home_x));
    if (mon->lurk)
    {
        if (mon_ai_archetype(r_ptr) == MAI_A_GUARDIAN)
            doc_printf(doc, "Provoked: chases you for %d more turns\n", mon->lurk);
        else
            doc_printf(doc, "Has lain in wait for %d turns\n", mon->lurk);
    }
}

errr mon_ai_parse_tactics(monster_race *r_ptr, char *buf)
{
    char *tokens[10];
    int   token_ct = z_string_split(buf, tokens, 10, "|");
    int   i, j;

    for (i = 0; i < token_ct; i++)
    {
        char *token = tokens[i];
        if (!strlen(token)) continue;
        if (streq(token, "COWARDLY")) { r_ptr->ai_traits |= MAI_T_COWARDLY; continue; }
        if (streq(token, "BRAVE")) { r_ptr->ai_traits |= MAI_T_BRAVE; continue; }
        for (j = 1; j < MAI_A_MAX; j++)
            if (streq(token, _arch_names[j])) break;
        if (j == MAI_A_MAX || r_ptr->ai_arch)
        {
            msg_format("Error: Unknown or second archetype %s.", token);
            return PARSE_ERROR_INVALID_FLAG;
        }
        r_ptr->ai_arch = j;
    }
    return 0;
}

static mon_ai_option_ptr _add_option(mon_ai_decision_ptr d, int kind, cptr why, int score)
{
    mon_ai_option_ptr opt;
    assert(d->option_ct < MAI_MAX_OPTIONS);
    opt = &d->options[d->option_ct++];
    opt->kind = kind;
    opt->score = score;
    opt->term_ct = 0;
    opt->terms[opt->term_ct].name = why;
    opt->terms[opt->term_ct++].value = score;
    return opt;
}

/* Record a term only when it changes the score, to keep explanations short */
static void _term(mon_ai_option_ptr opt, cptr why, int score)
{
    if (score == opt->score) return;
    opt->score = score;
    if (opt->term_ct < MAI_MAX_TERMS)
    {
        opt->terms[opt->term_ct].name = why;
        opt->terms[opt->term_ct++].value = score;
    }
}

/* The classic spell frequency, adjusted for glyphs, pack AI, anger,
 * point-blank frailty and stun. */
static void _score_cast(mon_ptr mon, mon_ai_option_ptr opt)
{
    pack_info_t  *pack_ptr = pack_info_ptr(mon->id);
    int           freq = opt->score;

    /* Increase spell frequency for pack AI or player glyphs of warding */
    if (is_glyph_grid(&cave[py][px]))
    {
        _term(opt, "Player on a glyph", MAX(30, freq + 10));
    }
    else if (pack_ptr)
    {
        switch (pack_ptr->ai)
        {
        case AI_SHOOT:
            _term(opt, "Pack AI: Shoot", MAX(30, freq + 15));
            break;
        case AI_MAINTAIN_DISTANCE:
            _term(opt, "Pack AI: Maintain Distance", freq + MIN(freq/2, 15));
            break;
        case AI_LURE:
            _term(opt, "Pack AI: Lure", freq + MIN(freq/2, 10));
            break;
        case AI_FEAR:
            _term(opt, "Pack AI: Fear", freq + MIN(freq/2, 10));
            break;
        case AI_GUARD_POS:
            _term(opt, "Pack AI: Guard Position", freq + MIN(freq/2, 10));
            break;
        }
    }
    freq = opt->score;

    /* Angry monsters will eventually spell if they get too pissed off.
     * Monsters are angered by distance attacks (spell casters/archers) */
    _term(opt, "Anger", freq + mon->anger);
    freq = opt->score;

    if (freq > 100) _term(opt, "Capped", 100);
    freq = opt->score;

    /* Berserkers would rather hit you */
    if (mon->cdis <= 1 && mon_ai_archetype(&r_info[mon->r_idx]) == MAI_A_BERSERKER)
    {
        _term(opt, "Berserk in melee", freq / 2);
        freq = opt->score;
    }

    /* XXX Adapt spell frequency down if monster is stunned (EXPERIMENTAL)
     * Sure, stunning effects fail rates, but not on innate spells (breaths).
     * In fact, distance stunning gives no benefit against big breathers ...
     * Try a sprite mindcrafter and you'll see what I mean. */
    if (MON_STUNNED(mon))
    {
        int s = MON_STUNNED(mon);
        int p = MAX(0, 100 - s);
        freq = freq * p / 100;
        if (freq < 1) freq = 1;
        _term(opt, "Stunned", freq);
    }
}

int mon_ai_speed(mon_ptr mon)
{
    int speed = mon->mspeed;
    if (ironman_nightmare) speed += 5;
    if (MON_FAST(mon)) speed += 10;
    speed -= monster_slow(mon);
    if (p_ptr->filibuster) speed -= SPEED_ADJ_FILIBUSTER;
    return speed;
}

/* How many neighbouring squares the monster could move on to (so a
 * retreating caster does not back itself into a dead end) */
static int _open_neighbours(monster_race *r_ptr, int y, int x)
{
    int d, ct = 0;
    for (d = 0; d < 8; d++)
    {
        int yy = y + ddy_ddd[d], xx = x + ddx_ddd[d];
        if (!in_bounds2(yy, xx)) continue;
        if (player_bold(yy, xx)) continue;
        if (monster_can_enter(yy, xx, r_ptr, 0)) ct++;
    }
    return ct;
}

static int _step_away_aux(mon_ptr mon, bool need_shot);

int mon_ai_step_away_dir(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];

    if (!mon_ai_race_kites(r_ptr)) return 0;

    /* A clearly faster player just follows and gets free hits: stepping back
     * only helps a monster that can keep up. (It may still blink.) */
    if (mon_ai_speed(mon) + 2 < p_ptr->pspeed) return 0;
    return _step_away_aux(mon, TRUE);
}

/* Skirmisher that hit the player last turn: step back out of reach. Only
 * worth it for a monster fast enough to come straight back in. */
static int _skirmish_dir(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];

    if (!mon->struck) return 0;
    if (mon_ai_archetype(r_ptr) != MAI_A_SKIRMISHER) return 0;
    if (mon_ai_speed(mon) < p_ptr->pspeed + 5) return 0;
    return _step_away_aux(mon, FALSE);
}

static int _step_away_aux(mon_ptr mon, bool need_shot)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           d, best_dir = 0, best_score = 0;

    if (mon->cdis > 1) return 0;
    if (!is_hostile(mon) || !is_aware(mon)) return 0;
    if (mon->id == p_ptr->riding) return 0;
    if (r_ptr->flags1 & RF1_NEVER_MOVE) return 0;
    if (MON_CONFUSED(mon) || MON_MONFEAR(mon) || MON_CSLEEP(mon)) return 0;

    for (d = 0; d < 8; d++)
    {
        int y = mon->fy + ddy_ddd[d];
        int x = mon->fx + ddx_ddd[d];
        int score;

        if (!in_bounds2(y, x)) continue;
        if (player_bold(y, x)) continue;
        if (cave[y][x].m_idx) continue;
        if (!monster_can_enter(y, x, r_ptr, 0)) continue;
        if (is_glyph_grid(&cave[y][x]) || is_mon_trap_grid(&cave[y][x])) continue;
        if (distance(py, px, y, x) < 2) continue;         /* still in melee */
        if (need_shot && !projectable(y, x, py, px)) continue;  /* keep a line of fire */

        score = 10 * distance(py, px, y, x) + _open_neighbours(r_ptr, y, x);
        if (score > best_score)
        {
            best_score = score;
            best_dir = ddd[d];
        }
    }
    return best_dir;
}

/* A frail caster already at a comfortable range with a clear shot should
 * not walk back into melee (the classic movement code runs at the player
 * whenever it is in view). */
static bool _hold_ok(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];

    if (mon->cdis < 2 || mon->cdis > 3) return FALSE;
    if (!is_hostile(mon) || !is_aware(mon)) return FALSE;
    if (mon->id == p_ptr->riding) return FALSE;
    if (MON_CONFUSED(mon) || MON_MONFEAR(mon) || MON_CSLEEP(mon)) return FALSE;
    if (!mon_ai_race_kites(r_ptr)) return FALSE;
    if (!projectable(mon->fy, mon->fx, py, px)) return FALSE;
    return TRUE;
}

/* Careful monsters keep their distance more, aggressive ones less */
static void _temper_term(mon_ptr mon, mon_ai_option_ptr opt, int amt)
{
    int t = mon_ai_temper(mon);
    if (t < 0) _term(opt, "Careful", opt->score + amt);
    else if (t > 0) _term(opt, "Aggressive", MAX(1, opt->score - amt));
}

/* How much a shaken, hurt monster wants to break off (0 = not at all) */
static int _retreat_score(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           morale = mon_ai_morale(mon);
    int           hp_pct = mon->hp * 100 / MAX(1, mon->maxhp);
    int           limit = (r_ptr->ai_traits & MAI_T_COWARDLY) ? 70 : 50;
    int           score;

    if (!is_hostile(mon) || mon->id == p_ptr->riding) return 0;
    if (!mon_ai_has_contact(mon)) return 0;
    if (r_ptr->flags1 & RF1_NEVER_MOVE) return 0;
    if (r_ptr->flags3 & RF3_NO_FEAR) return 0;
    if (MON_MONFEAR(mon) || MON_CONFUSED(mon)) return 0;  /* the old fear code handles that */
    if (mon_ai_archetype(r_ptr) == MAI_A_BERSERKER) return 0;
    if (mon->intent == MAI_I_REGROUP)
    {
        /* Still being chased after 10 turns of running: turn and fight */
        if (!mon->intent_timer)
        {
            mon->intent = MAI_I_NONE;
            return 0;
        }
        if (hp_pct < 70) return 40;
    }
    if (mon->shouted) return 0;  /* already broke off once: fights to the end now */
    if (hp_pct >= limit || morale >= limit) return 0;
    score = (limit - morale) + (limit - hp_pct);
    score -= 10 * mon_ai_temper(mon);  /* careful +10, aggressive -10 */
    return MAX(0, score);
}

/* Ambusher heard (or smelt) but unseen, not yet close: stay hidden */
static bool _lurk_ok(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];

    if (mon_ai_archetype(r_ptr) != MAI_A_AMBUSHER) return FALSE;
    if (!is_hostile(mon) || mon->id == p_ptr->riding) return FALSE;
    if (mon->ai_contact != MAI_C_HEARING && mon->ai_contact != MAI_C_SCENT) return FALSE;
    if (mon->cdis <= 2) return FALSE;
    if (mon->lurk >= 40) return FALSE;  /* patience runs out */
    if (MON_CONFUSED(mon) || MON_MONFEAR(mon)) return FALSE;
    if (projectable(mon->fy, mon->fx, py, px)) return FALSE;
    return TRUE;
}

/* Guardians keep to within this many squares of their post */
#define _GUARD_LEASH 7

static bool _off_leash(mon_ptr mon, int y, int x)
{
    if (!mon->home_y && !mon->home_x) return FALSE;
    return distance(mon->home_y, mon->home_x, y, x) > _GUARD_LEASH;
}

static int _home_step(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    if ( (r_ptr->flags2 & (RF2_PASS_WALL | RF2_KILL_WALL))
      || los(mon->fy, mon->fx, mon->home_y, mon->home_x) )
    {
        return _greedy_step(mon, mon->home_y, mon->home_x);
    }
    return _path_step(mon, mon->home_y, mon->home_x);
}

/* Guardian: the player is beyond its leash. Returns TRUE and sets *dir to a
 * step home (0 = already there: wait) */
static bool _guard_ok(mon_ptr mon, int *dir)
{
    monster_race *r_ptr = &r_info[mon->r_idx];

    if (mon_ai_archetype(r_ptr) != MAI_A_GUARDIAN) return FALSE;
    if (!is_hostile(mon) || mon->id == p_ptr->riding) return FALSE;
    if (mon->cdis <= 1) return FALSE;  /* in reach: fight */
    if (r_ptr->flags1 & RF1_NEVER_MOVE) return FALSE;
    if (MON_CONFUSED(mon) || MON_MONFEAR(mon)) return FALSE;
    if (mon->lurk) return FALSE;  /* provoked */
    if (!_off_leash(mon, py, px)) return FALSE;
    *dir = distance(mon->fy, mon->fx, mon->home_y, mon->home_x) > 1 ? _home_step(mon) : 0;
    return TRUE;
}

static int _squad_options(mon_ptr mon, mon_ai_decision_ptr d, int total);

void mon_ai_decide(mon_ptr mon, bool cast_blocked, mon_ai_decision_ptr d)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           total = 100;

    d->mon = mon;
    d->option_ct = 0;
    d->choice = -1;
    d->step_dir = 0;

    /* The options are ordered and scored so that they add up to 100 and
     * mon_ai_choose's randint1(100) matches the classic
     * "randint1(100) <= freq" test exactly. */
    if (r_ptr->spells && !cast_blocked)
    {
        mon_ai_option_ptr cast = _add_option(d, MAI_CAST, "Race spell frequency", r_ptr->spells->freq);
        _score_cast(mon, cast);
        total -= cast->score;
    }

    /* A frail caster in melee range would rather back off and keep casting
     * than trade blows. Only offered when a good square exists, so every
     * other monster sees exactly the classic cast-or-act choice. */
    if (total > 0)
    {
        int  dir = mon_ai_step_away_dir(mon), guard_dir = 0;
        bool skirmish = FALSE;
        if (!dir)
        {
            dir = _skirmish_dir(mon);
            skirmish = dir ? TRUE : FALSE;
        }
        if (dir)
        {
            mon_ai_option_ptr step = skirmish
                ? _add_option(d, MAI_STEP_AWAY, "Skirmisher: hit and run", 60)
                : _add_option(d, MAI_STEP_AWAY, "Frail in melee, square free", 60);
            if (r_ptr->flags2 & RF2_SMART)
                _term(step, "Smart", step->score + 15);
            if (mon->hp < mon->maxhp / 2)
                _term(step, "Badly hurt", step->score + 15);
            _temper_term(mon, step, 15);
            if (step->score > total)
                _term(step, "Capped", total);
            total -= step->score;
            d->step_dir = dir;
        }
        else if (_retreat_score(mon))
        {
            mon_ai_option_ptr opt = _add_option(d, MAI_RETREAT, "Shaken and hurt", _retreat_score(mon));
            if (mon->intent == MAI_I_REGROUP)
                _term(opt, "Already regrouping", MAX(opt->score, total * 4 / 5));
            if (opt->score > total)
                _term(opt, "Capped", total);
            total -= opt->score;
        }
        else if (_hold_ok(mon))
        {
            mon_ai_option_ptr hold = _add_option(d, MAI_HOLD, "Frail, at range with a shot", 70);
            if (r_ptr->flags2 & RF2_SMART)
                _term(hold, "Smart", hold->score + 15);
            _temper_term(mon, hold, 15);
            if (hold->score > total)
                _term(hold, "Capped", total);
            total -= hold->score;
        }
        else if (_lurk_ok(mon))
        {
            mon_ai_option_ptr lurk = _add_option(d, MAI_LURK, "Ambusher: heard, unseen", 80);
            _temper_term(mon, lurk, 15);
            if (lurk->score > total)
                _term(lurk, "Capped", total);
            total -= lurk->score;
        }
        else if (_guard_ok(mon, &guard_dir))
        {
            /* Certain: a guardian does not chase past its leash */
            _add_option(d, MAI_GUARD, "Guardian: you are past its leash", total);
            d->step_dir = guard_dir;
            total = 0;
        }
    }
    total -= _squad_options(mon, d, total);
    _add_option(d, MAI_PHYSICAL, "Whenever not casting", total);
}

int mon_ai_choose(mon_ai_decision_ptr d)
{
    int i, total = 0, roll;

    assert(d->option_ct > 0);
    if (d->option_ct == 1)
    {
        d->choice = 0;
        return d->options[0].kind;
    }
    for (i = 0; i < d->option_ct; i++)
        total += d->options[i].score;
    if (total <= 0)
    {
        d->choice = d->option_ct - 1;
        return d->options[d->choice].kind;
    }
    roll = randint1(total);
    for (i = 0; i < d->option_ct; i++)
    {
        roll -= d->options[i].score;
        if (roll <= 0) break;
    }
    if (i >= d->option_ct) i = d->option_ct - 1;
    d->choice = i;
    return d->options[i].kind;
}

int mon_ai_cast_score(mon_ptr mon)
{
    mon_ai_decision_t d;
    int               i;
    mon_ai_decide(mon, FALSE, &d);
    for (i = 0; i < d.option_ct; i++)
        if (d.options[i].kind == MAI_CAST) return d.options[i].score;
    return 0;
}

void mon_ai_doc(mon_ai_decision_ptr d, doc_ptr doc)
{
    int i, j, total = 0;
    for (i = 0; i < d->option_ct; i++)
        total += d->options[i].score;
    for (i = 0; i < d->option_ct; i++)
    {
        mon_ai_option_ptr opt = &d->options[i];
        int               pct = total ? opt->score * 100 / total : (i == d->option_ct - 1 ? 100 : 0);
        doc_printf(doc, "<color:%c>%3d%%</color> %s\n", i == d->choice ? 'y' : 'R', pct, mon_ai_kind_name(opt->kind));
        for (j = 0; j < opt->term_ct; j++)
        {
            mon_ai_term_t *t = &opt->terms[j];
            if (j == 0)
                doc_printf(doc, "<tab:6><color:D>%-30.30s %3d</color>\n", t->name, t->value);
            else
                doc_printf(doc, "<tab:6><color:D>%-30.30s %+3d -> %d</color>\n", t->name, t->value - opt->terms[j-1].value, t->value);
        }
    }
}

/*************************************************************************
 * Perception
 ************************************************************************/
static s32b _noise_turn = -1000;
static int  _noise_level = 0;

cptr mon_ai_state_name(int state)
{
    switch (state)
    {
    case MAI_S_UNSET: return "Unset";
    case MAI_S_HUNTING: return "Hunting";
    case MAI_S_TRACKING: return "Tracking";
    case MAI_S_SEARCHING: return "Searching";
    case MAI_S_IDLE: return "Idle";
    }
    return "?";
}

cptr mon_ai_contact_name(int contact)
{
    switch (contact)
    {
    case MAI_C_NONE: return "none";
    case MAI_C_ADJACENT: return "adjacent";
    case MAI_C_SIGHT: return "sight";
    case MAI_C_HEARING: return "hearing";
    case MAI_C_SCENT: return "scent";
    }
    return "?";
}

/* A noisy action is heard for about two player turns */
void mon_ai_player_noise(int loudness)
{
    if (game_turn - _noise_turn > 20 || loudness > _noise_level)
        _noise_level = loudness;
    _noise_turn = game_turn;
}

static int _noise_bonus(void)
{
    if (game_turn - _noise_turn <= 20) return _noise_level;
    return 0;
}

/* How many steps away (along the flow map, i.e. by walkable path) the
 * monster can hear the player: its alertness radius minus the player's
 * stealth, plus any noise the player just made. */
int mon_ai_hearing_range(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           range = r_ptr->aaf - p_ptr->skills.stl + _noise_bonus();
    if (r_ptr->flags2 & RF2_SMART) range += 2;
    if (range < 2) range = 2;
    return range;
}

static int _contact(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    cave_type    *c_ptr = &cave[mon->fy][mon->fx];

    if (mon->cdis <= 1) return MAI_C_ADJACENT;
    if (mon->cdis <= MAX_SIGHT && los(mon->fy, mon->fx, py, px)) return MAI_C_SIGHT;
    if (c_ptr->dist && c_ptr->dist <= mon_ai_hearing_range(mon)) return MAI_C_HEARING;
    if ( (r_ptr->flags3 & RF3_ANIMAL)
      && c_ptr->when
      && cave[py][px].when - c_ptr->when < 60 )
    {
        return MAI_C_SCENT;
    }
    return MAI_C_NONE;
}

static void _know_player(mon_ptr mon)
{
    mon->lk_y = py;
    mon->lk_x = px;
    mon->lk_turn = game_turn;
}

void mon_ai_alert(mon_ptr mon)
{
    if (!is_hostile(mon)) return;
    _know_player(mon);
    if (mon->ai_state != MAI_S_HUNTING)
        mon->ai_state = MAI_S_TRACKING;
}

bool mon_ai_has_contact(mon_ptr mon)
{
    if (!is_hostile(mon)) return TRUE;           /* pets and friends: unchanged */
    if (mon->ai_state == MAI_S_UNSET) return TRUE; /* not evaluated yet */
    return mon->ai_contact != MAI_C_NONE;
}

void mon_ai_perceive(mon_ptr mon)
{
    pack_info_t *pack = pack_info_ptr(mon->id);
    int          c;

    if (!is_hostile(mon)) return;
    if (mon_ai_tracked(mon)) mon_ai_stats.states[mon->ai_state]++;

    /* Its post, for guardians: where it first became active */
    if (!mon->home_y && !mon->home_x)
    {
        mon->home_y = mon->fy;
        mon->home_x = mon->fx;
    }

    c = _contact(mon);
    mon->ai_contact = c;

    /* Provoked guardians calm down; an ambusher that has been lying in
     * wait comes out when it sees you */
    if (mon_ai_archetype(&r_info[mon->r_idx]) == MAI_A_GUARDIAN)
    {
        if (mon->lurk) mon->lurk--;
    }
    else if ((c == MAI_C_SIGHT || c == MAI_C_ADJACENT) && mon->lurk)
    {
        if (mon->lurk >= 3 && mon_show_msg(mon))
        {
            char m_name[MAX_NLEN];
            monster_desc(m_name, mon, 0);
            msg_format("%^s springs from hiding!", m_name);
        }
        mon->lurk = 0;
    }

    if (c)
    {
        _know_player(mon);
        mon->ai_state = MAI_S_HUNTING;
        if (pack)
        {
            /* Shouts, howls, signals: the pack shares what it perceives */
            pack->lk_y = py;
            pack->lk_x = px;
            pack->lk_turn = game_turn;
        }
        return;
    }

    /* Morale recovers out of contact */
    if (mon->morale_lost) mon->morale_lost = MAX(0, mon->morale_lost - 3);

    /* No contact. A newly woken, loaded or placed monster knew roughly
     * where the player was. */
    if (mon->ai_state == MAI_S_UNSET)
    {
        _know_player(mon);
        mon->ai_state = MAI_S_TRACKING;
    }
    else if (mon->ai_state == MAI_S_HUNTING)
        mon->ai_state = MAI_S_TRACKING;

    /* A packmate knows something newer */
    if (pack && pack->lk_turn > mon->lk_turn)
    {
        mon->lk_y = pack->lk_y;
        mon->lk_x = pack->lk_x;
        mon->lk_turn = pack->lk_turn;
        mon->ai_state = MAI_S_TRACKING;
    }

    switch (mon->ai_state)
    {
    case MAI_S_TRACKING:
        if (distance(mon->fy, mon->fx, mon->lk_y, mon->lk_x) <= 1)
        {
            mon->ai_state = MAI_S_SEARCHING;
            mon->ai_timer = 20;
        }
        break;
    case MAI_S_SEARCHING:
        if (mon->ai_timer) mon->ai_timer--;
        else mon->ai_state = MAI_S_IDLE;
        break;
    }
}

/* Can this monster walk on (y, x), ignoring other monsters? */
static bool _passable(monster_race *r_ptr, int y, int x)
{
    cave_type *c_ptr;
    if (!in_bounds(y, x)) return FALSE;
    if (player_bold(y, x)) return FALSE;
    c_ptr = &cave[y][x];
    if (is_closed_door(c_ptr->feat))
        return (r_ptr->flags2 & (RF2_OPEN_DOOR | RF2_BASH_DOOR)) ? TRUE : FALSE;
    return monster_can_cross_terrain(c_ptr->feat, r_ptr, 0);
}

/* First step of a shortest walkable path to (ty, tx), searching at most
 * ~50 steps out. Returns a keypad direction or 0. */
#define _PATH_MAX_NODES 6000
static u16b _path_stamp[MAX_HGT][MAX_WID];
static byte _path_from[MAX_HGT][MAX_WID];  /* ddd index the cell was entered by */
static u16b _path_gen = 0;
static byte _path_qy[_PATH_MAX_NODES], _path_qx[_PATH_MAX_NODES];

static int _path_step(mon_ptr mon, int ty, int tx)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           head = 0, tail = 0, d;

    if (++_path_gen == 0)
    {
        memset(_path_stamp, 0, sizeof(_path_stamp));
        _path_gen = 1;
    }
    _path_stamp[mon->fy][mon->fx] = _path_gen;
    _path_qy[tail] = mon->fy;
    _path_qx[tail++] = mon->fx;

    while (head < tail)
    {
        int y = _path_qy[head], x = _path_qx[head++];
        if (y == ty && x == tx)
        {
            /* Walk back to the cell next to the monster */
            while (1)
            {
                int dd = _path_from[y][x];
                int py2 = y - ddy_ddd[dd], px2 = x - ddx_ddd[dd];
                if (py2 == mon->fy && px2 == mon->fx) return ddd[dd];
                y = py2;
                x = px2;
            }
        }
        if (distance(mon->fy, mon->fx, y, x) > 50) continue;
        for (d = 0; d < 8; d++)
        {
            int ny = y + ddy_ddd[d], nx = x + ddx_ddd[d];
            if (!in_bounds2(ny, nx)) continue;
            if (_path_stamp[ny][nx] == _path_gen) continue;
            if (!(ny == ty && nx == tx) && !_passable(r_ptr, ny, nx)) continue;
            _path_stamp[ny][nx] = _path_gen;
            _path_from[ny][nx] = d;
            if (tail >= _PATH_MAX_NODES) return 0;
            _path_qy[tail] = ny;
            _path_qx[tail++] = nx;
        }
    }
    return 0;
}

/* Step straight at the target (wall-passers, or a clear line) */
static int _greedy_step(mon_ptr mon, int ty, int tx)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           d, best = 0, best_dist = distance(mon->fy, mon->fx, ty, tx);
    bool          walls = (r_ptr->flags2 & (RF2_PASS_WALL | RF2_KILL_WALL)) ? TRUE : FALSE;

    for (d = 0; d < 8; d++)
    {
        int y = mon->fy + ddy_ddd[d], x = mon->fx + ddx_ddd[d];
        int dist;
        if (!in_bounds2(y, x)) continue;
        if (!walls && !_passable(r_ptr, y, x)) continue;
        dist = distance(y, x, ty, tx);
        if (dist < best_dist)
        {
            best_dist = dist;
            best = ddd[d];
        }
    }
    return best;
}

bool mon_ai_track_moves(mon_ptr mon, int *mm)
{
    monster_race *r_ptr = &r_info[mon->r_idx];

    /* Regrouping out of sight: stay put and recover (mon_ai_intent_turn) */
    if (mon->intent == MAI_I_REGROUP) return FALSE;

    /* Guardians don't follow you away from their post */
    if ( mon_ai_archetype(r_ptr) == MAI_A_GUARDIAN
      && !(r_ptr->flags1 & RF1_NEVER_MOVE)
      && !mon->lurk
      && (mon->ai_state == MAI_S_IDLE || _off_leash(mon, mon->lk_y, mon->lk_x)) )
    {
        int dir = 0;
        if (distance(mon->fy, mon->fx, mon->home_y, mon->home_x) > 1)
            dir = _home_step(mon);
        if (!dir) return FALSE;
        mm[0] = dir;
        mm[1] = 0;
        return TRUE;
    }

    if (mon->ai_state == MAI_S_TRACKING)
    {
        int dir;
        if ( (r_ptr->flags2 & (RF2_PASS_WALL | RF2_KILL_WALL))
          || los(mon->fy, mon->fx, mon->lk_y, mon->lk_x) )
        {
            dir = _greedy_step(mon, mon->lk_y, mon->lk_x);
        }
        else
            dir = _path_step(mon, mon->lk_y, mon->lk_x);

        if (dir)
        {
            mm[0] = dir;
            mm[1] = 0;
            return TRUE;
        }
        /* Can't get there: look around here instead */
        mon->ai_state = MAI_S_SEARCHING;
        mon->ai_timer = 20;
    }

    if (mon->ai_state == MAI_S_SEARCHING)
    {
        mm[0] = mm[1] = mm[2] = mm[3] = 5;
        return TRUE;
    }

    /* Idle: mostly stay put, sometimes wander */
    if (one_in_(4))
    {
        mm[0] = mm[1] = mm[2] = mm[3] = 5;
        return TRUE;
    }
    return FALSE;
}

/*************************************************************************
 * Intents and morale
 ************************************************************************/
cptr mon_ai_intent_name(int intent)
{
    switch (intent)
    {
    case MAI_I_NONE: return "None";
    case MAI_I_CHARGE: return "Charging a spell";
    case MAI_I_REGROUP: return "Regrouping";
    }
    return "?";
}

int mon_ai_morale(mon_ptr mon)
{
    return 100 - mon->morale_lost;
}

static bool _leader_near(mon_ptr mon);

static void _lose_morale(mon_ptr mon, int amt)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    if (amt <= 0) return;
    if (r_ptr->flags3 & RF3_NO_FEAR) return;
    if (r_ptr->flags1 & RF1_UNIQUE) amt /= 2;
    if (r_ptr->ai_traits & MAI_T_BRAVE) amt /= 2;
    if (r_ptr->ai_traits & MAI_T_COWARDLY) amt = amt * 3 / 2;
    if (mon_ai_archetype(r_ptr) == MAI_A_BERSERKER) amt /= 2;
    if (mon_ai_courage(mon) < 0) amt = amt * 4 / 3;
    else if (mon_ai_courage(mon) > 0) amt = amt * 2 / 3;
    if (_leader_near(mon)) amt /= 2;  /* a leader in sight steadies the ranks */
    mon->morale_lost = MIN(100, mon->morale_lost + amt);
}

static void _interrupt(mon_ptr mon, cptr how)
{
    if (mon->intent != MAI_I_CHARGE) return;
    mon->intent = MAI_I_NONE;
    if (mon_ai_tracked(mon)) mon_ai_stats.interrupts++;
    if (mon_show_msg(mon))
    {
        char m_name[MAX_NLEN];
        monster_desc(m_name, mon, 0);
        msg_format("%^s %s", m_name, how);
    }
}

void mon_ai_on_hurt(mon_ptr mon, int dam)
{
    int pct;
    if (dam <= 0 || !mon->maxhp) return;
    pct = MIN(100, dam * 100 / mon->maxhp);

    /* Being hurt is unnerving: morale drops by the share of health lost */
    _lose_morale(mon, pct);

    /* A guardian shot at from beyond its leash comes after you for a while
     * (mon->lurk counts down the provocation for guardians) */
    if (mon_ai_archetype(&r_info[mon->r_idx]) == MAI_A_GUARDIAN && _off_leash(mon, py, px))
        mon->lurk = 20;

    /* A solid hit breaks the concentration of a charging monster */
    if (mon->intent == MAI_I_CHARGE && pct >= 10)
        _interrupt(mon, "is interrupted!");
}

void mon_ai_on_disabled(mon_ptr mon)
{
    if (mon->intent == MAI_I_CHARGE)
        _interrupt(mon, "loses its focus!");
}

void mon_ai_on_ally_death(mon_ptr dead)
{
    pack_info_t *pack = pack_info_ptr(dead->id);
    int          i;

    if (!pack) return;
    for (i = 1; i < m_max; i++)
    {
        mon_ptr mate = &m_list[i];
        if (i == dead->id || !mate->r_idx || mate->pack_idx != dead->pack_idx) continue;
        if (distance(mate->fy, mate->fx, dead->fy, dead->fx) > 15) continue;
        _lose_morale(mate, pack->leader_idx == dead->id ? 30 : 12);
    }
}

void mon_ai_start_charge(mon_ptr mon, int type, int effect)
{
    mon->intent = MAI_I_CHARGE;
    mon->intent_timer = 3;  /* released next turn; held up to 2 more if it has no shot */
    mon->intent_type = type + 1;
    mon->intent_effect = effect;
    if (mon_ai_tracked(mon)) mon_ai_stats.charges++;
}

/* Wake sleeping monsters nearby and tell them where the player is */
static void _shout_for_help(mon_ptr mon)
{
    int i, woke = 0;
    for (i = 1; i < m_max; i++)
    {
        mon_ptr other = &m_list[i];
        if (i == mon->id || !other->r_idx || !is_hostile(other)) continue;
        if (distance(mon->fy, mon->fx, other->fy, other->fx) > 6) continue;
        if (MON_CSLEEP(other))
        {
            (void)set_monster_csleep(i, 0);
            woke++;
        }
        mon_ai_alert(other);
    }
    mon->shouted = TRUE;
    if (mon_ai_tracked(mon)) mon_ai_stats.shouts++;
    if (woke && mon_show_msg(mon))
    {
        char m_name[MAX_NLEN];
        monster_desc(m_name, mon, 0);
        msg_format("%^s shouts for help!", m_name);
    }
}

bool mon_ai_retreat_moves(mon_ptr mon, int *mm)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           d, best_dir = 0, best_score = -1000;

    if (!mon->shouted)
    {
        _shout_for_help(mon);
        mon->intent_timer = 10;  /* turns allowed to break contact */
    }
    else if (mon->intent_timer)
        mon->intent_timer--;
    if (mon_ai_tracked(mon)) mon_ai_stats.retreats++;

    for (d = 0; d < 8; d++)
    {
        int y = mon->fy + ddy_ddd[d], x = mon->fx + ddx_ddd[d];
        int score;
        if (!in_bounds2(y, x)) continue;
        if (!monster_can_enter(y, x, r_ptr, 0) && !is_closed_door(cave[y][x].feat)) continue;
        score = 10 * (distance(py, px, y, x) - mon->cdis);
        if (!los(y, x, py, px)) score += 15;       /* out of sight is safer */
        score += _open_neighbours(r_ptr, y, x);   /* avoid dead ends */
        if (score > best_score)
        {
            best_score = score;
            best_dir = ddd[d];
        }
    }
    if (!best_dir || best_score < 0) return FALSE;  /* cornered: fight on */
    mm[0] = best_dir;
    mm[1] = 0;
    return TRUE;
}

bool mon_ai_intent_turn(mon_ptr mon)
{
    if (mon->intent == MAI_I_CHARGE)
    {
        if (MON_CONFUSED(mon) || MON_STUNNED(mon))
        {
            mon_ai_on_disabled(mon);
            return FALSE;
        }
        if (mon_spell_cast_charged(mon, mon->intent_type - 1, mon->intent_effect))
        {
            if (mon_ai_tracked(mon)) mon_ai_stats.releases++;
            mon->intent = MAI_I_NONE;
            return TRUE;
        }
        /* No shot this turn: hold the charge for a little while */
        if (mon->intent_timer) mon->intent_timer--;
        if (!mon->intent_timer)
        {
            mon->intent = MAI_I_NONE;
            if (mon_show_msg(mon))
            {
                char m_name[MAX_NLEN];
                monster_desc(m_name, mon, 0);
                msg_format("%^s lets the gathered power fade.", m_name);
            }
            return FALSE;
        }
        return TRUE;
    }

    if (mon->intent == MAI_I_REGROUP)
    {
        /* Out of contact: lick wounds until ready, then go back */
        if (!mon_ai_has_contact(mon))
        {
            if (mon->hp < mon->maxhp)
                (void)hp_mon(mon, MAX(1, mon->maxhp / 100), FALSE);
            if (mon->hp * 10 >= mon->maxhp * 7 && mon_ai_morale(mon) >= 60)
            {
                /* Back to the fight. (shouted stays set: one regroup per
                 * monster, so a faster monster can't flee-heal forever.) */
                mon->intent = MAI_I_NONE;
                mon->ai_state = MAI_S_TRACKING;  /* return to the last known position */
            }
            return FALSE;
        }
    }
    return FALSE;
}

/*************************************************************************
 * Squads
 ************************************************************************/
cptr mon_ai_role_name(int role)
{
    switch (role)
    {
    case MAI_R_NONE: return "None";
    case MAI_R_LEADER: return "Leader";
    case MAI_R_FRONTLINE: return "Frontline";
    case MAI_R_FLANKER: return "Flanker";
    case MAI_R_ARTILLERY: return "Artillery";
    case MAI_R_SUPPORT: return "Support";
    }
    return "?";
}

cptr mon_ai_plan_name(int plan)
{
    switch (plan)
    {
    case MAI_P_NONE: return "None";
    case MAI_P_SURROUND: return "Surround";
    case MAI_P_CHOKE: return "Hold the chokepoint";
    }
    return "?";
}

int mon_ai_role(mon_ptr mon)
{
    pack_info_t  *pack = pack_info_ptr(mon->id);
    monster_race *r_ptr = &r_info[mon->r_idx];

    if (!pack) return MAI_R_NONE;
    if (pack->leader_idx == mon->id) return MAI_R_LEADER;
    if (!mon->ai_role)
    {
        int arch = mon_ai_archetype(r_ptr);
        if (arch == MAI_A_SUPPORT || mon_race_has_healing(r_ptr))
            mon->ai_role = MAI_R_SUPPORT;
        else if (arch == MAI_A_ARTILLERY)
            mon->ai_role = MAI_R_ARTILLERY;
        else if (arch == MAI_A_SKIRMISHER)
            mon->ai_role = MAI_R_FLANKER;
        else if (arch == MAI_A_GUARDIAN || arch == MAI_A_BERSERKER)
            mon->ai_role = MAI_R_FRONTLINE;
        else if (mon->id % 3 == 0)
            mon->ai_role = MAI_R_FLANKER;
        else
            mon->ai_role = MAI_R_FRONTLINE;
    }
    return mon->ai_role;
}

/* Free squares next to the player that a monster could stand on */
static int _open_around_player(void)
{
    int d, ct = 0;
    for (d = 0; d < 8; d++)
    {
        int y = py + ddy_ddd[d], x = px + ddx_ddd[d];
        if (!in_bounds2(y, x)) continue;
        if (cave_have_flag_bold(y, x, FF_MOVE)) ct++;
    }
    return ct;
}

int mon_ai_squad_plan(mon_ptr mon)
{
    pack_info_t *pack = pack_info_ptr(mon->id);
    int          plan;

    if (!pack || pack->ai != AI_SEEK || pack->count < 3 || !is_hostile(mon)) return MAI_P_NONE;
    if (pack->plan_turn == player_turn) return pack->plan;

    plan = (_open_around_player() >= 5) ? MAI_P_SURROUND : MAI_P_CHOKE;
    if (!mon_ai_has_contact(mon)) plan = pack->plan;  /* keep the plan until someone sees the player */

    /* The leader commits the squad */
    if (plan != MAI_P_NONE && plan != pack->plan && pack->leader_idx)
    {
        mon_ptr leader = &m_list[pack->leader_idx];
        if (leader->r_idx && mon_ai_has_contact(leader))
        {
            if (mon_ai_tracked(leader)) mon_ai_stats.barks++;
            if (mon_show_msg(leader))
            {
                char m_name[MAX_NLEN];
                monster_desc(m_name, leader, 0);
                msg_format("%^s barks orders!", m_name);
            }
        }
    }
    pack->plan = plan;
    pack->plan_turn = player_turn;
    return plan;
}

/* Is a living leader close by and in view? It steadies the ranks. */
static bool _leader_near(mon_ptr mon)
{
    pack_info_t *pack = pack_info_ptr(mon->id);
    mon_ptr      leader;
    if (!pack || !pack->leader_idx || pack->leader_idx == mon->id) return FALSE;
    leader = &m_list[pack->leader_idx];
    if (!leader->r_idx) return FALSE;
    if (distance(mon->fy, mon->fx, leader->fy, leader->fx) > 10) return FALSE;
    return los(mon->fy, mon->fx, leader->fy, leader->fx);
}

/* Flanker: the free square next to the player farthest from where the
 * packmates already are, and the first step towards it (0 if none). */
static int _flank_dir(mon_ptr mon)
{
    int i, d, fy = 0, fx = 0, ct = 0, best_d = -1, ty = 0, tx = 0;

    for (i = 1; i < m_max; i++)
    {
        mon_ptr mate = &m_list[i];
        if (i == mon->id || !mate->r_idx || mate->pack_idx != mon->pack_idx) continue;
        if (mate->cdis > 2) continue;
        fy += mate->fy;
        fx += mate->fx;
        ct++;
    }
    if (!ct) return 0;  /* nobody engaging yet: just close in */
    fy /= ct;
    fx /= ct;

    /* Worth going round only for a square well away from the others that
     * is not a long detour: two squares of spread per extra step. */
    for (d = 0; d < 8; d++)
    {
        int y = py + ddy_ddd[d], x = px + ddx_ddd[d], spread, walk, score;
        if (!in_bounds2(y, x) || cave[y][x].m_idx) continue;
        if (!_passable(&r_info[mon->r_idx], y, x)) continue;
        spread = distance(fy, fx, y, x);
        walk = distance(mon->fy, mon->fx, y, x);
        if (spread < 2 || walk > mon->cdis + 2) continue;
        score = 2 * spread - walk;
        if (score > best_d)
        {
            best_d = score;
            ty = y;
            tx = x;
        }
    }
    if (best_d < 0) return 0;  /* no square on the far side */
    d = _path_step(mon, ty, tx);
    if (!d) return 0;

    /* A packmate in the way: close in normally rather than stand still */
    {
        int y = mon->fy + ddy[d], x = mon->fx + ddx[d];
        if (!in_bounds2(y, x) || cave[y][x].m_idx) return 0;
    }
    return d;
}

/* Squad options for mon_ai_decide(). Returns the score used. */
static int _squad_options(mon_ptr mon, mon_ai_decision_ptr d, int total)
{
    int plan, role;

    if (total <= 0 || !mon_ai_has_contact(mon) || mon->cdis <= 1) return 0;
    if (MON_CONFUSED(mon) || MON_MONFEAR(mon)) return 0;
    if (r_info[mon->r_idx].flags1 & RF1_NEVER_MOVE) return 0;
    plan = mon_ai_squad_plan(mon);
    if (plan == MAI_P_NONE) return 0;
    role = mon_ai_role(mon);

    if (plan == MAI_P_SURROUND && role == MAI_R_FLANKER && mon->cdis <= 8)
    {
        int dir = _flank_dir(mon);
        if (dir)
        {
            mon_ai_option_ptr opt = _add_option(d, MAI_FLANK, "Squad: flank the player", 70);
            if (opt->score > total) _term(opt, "Capped", total);
            d->step_dir = dir;
            return opt->score;
        }
    }
    if ( plan == MAI_P_CHOKE
      && (role == MAI_R_FRONTLINE || role == MAI_R_FLANKER)
      && mon->cdis <= 4 )
    {
        int dd, free_ct = 0;
        for (dd = 0; dd < 8; dd++)
        {
            int y = py + ddy_ddd[dd], x = px + ddx_ddd[dd];
            if (in_bounds2(y, x) && !cave[y][x].m_idx && cave_have_flag_bold(y, x, FF_MOVE)) free_ct++;
        }
        if (!free_ct)
        {
            mon_ai_option_ptr opt = _add_option(d, MAI_WAIT, "Squad: no way through, wait", 70);
            if (opt->score > total) _term(opt, "Capped", total);
            return opt->score;
        }
    }
    return 0;
}
