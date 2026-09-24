/* Monster turn decisions: candidate actions scored by named terms.
 * See mon_ai.h. */

#include "angband.h"

#include <assert.h>

mon_ai_stats_t mon_ai_stats = {0};

cptr mon_ai_kind_name(int kind)
{
    switch (kind)
    {
    case MAI_CAST: return "Cast a spell";
    case MAI_STEP_AWAY: return "Step away from the player";
    case MAI_HOLD: return "Hold position at range";
    case MAI_PHYSICAL: return "Move or melee";
    }
    return "?";
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
    monster_race *r_ptr = &r_info[mon->r_idx];
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

int mon_ai_step_away_dir(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           d, best_dir = 0, best_score = 0;

    if (mon->cdis > 1) return 0;
    if (!is_hostile(mon) || !is_aware(mon)) return 0;
    if (mon->id == p_ptr->riding) return 0;
    if (r_ptr->flags1 & RF1_NEVER_MOVE) return 0;
    if (MON_CONFUSED(mon) || MON_MONFEAR(mon) || MON_CSLEEP(mon)) return 0;
    if (!mon_race_weak_melee(r_ptr) || !mon_race_has_attack_spell(r_ptr)) return 0;

    /* A clearly faster player just follows and gets free hits: stepping back
     * only helps a monster that can keep up. (It may still blink.) */
    if (mon_ai_speed(mon) + 2 < p_ptr->pspeed) return 0;

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
        if (!projectable(y, x, py, px)) continue;         /* keep a line of fire */

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
    if (!mon_race_weak_melee(r_ptr) || !mon_race_has_attack_spell(r_ptr)) return FALSE;
    if (!projectable(mon->fy, mon->fx, py, px)) return FALSE;
    return TRUE;
}

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
        int dir = mon_ai_step_away_dir(mon);
        if (dir)
        {
            mon_ai_option_ptr step = _add_option(d, MAI_STEP_AWAY, "Frail in melee, square free", 60);
            if (r_ptr->flags2 & RF2_SMART)
                _term(step, "Smart", step->score + 15);
            if (mon->hp < mon->maxhp / 2)
                _term(step, "Badly hurt", step->score + 15);
            if (step->score > total)
                _term(step, "Capped", total);
            total -= step->score;
            d->step_dir = dir;
        }
        else if (_hold_ok(mon))
        {
            mon_ai_option_ptr hold = _add_option(d, MAI_HOLD, "Frail, at range with a shot", 70);
            if (r_ptr->flags2 & RF2_SMART)
                _term(hold, "Smart", hold->score + 15);
            if (hold->score > total)
                _term(hold, "Capped", total);
            total -= hold->score;
        }
    }
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
