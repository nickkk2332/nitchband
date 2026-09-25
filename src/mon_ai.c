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

/* How much a shaken, hurt monster wants to break off (0 = not at all) */
static int _retreat_score(mon_ptr mon)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    int           morale = mon_ai_morale(mon);
    int           hp_pct = mon->hp * 100 / MAX(1, mon->maxhp);

    if (!is_hostile(mon) || mon->id == p_ptr->riding) return 0;
    if (!mon_ai_has_contact(mon)) return 0;
    if (r_ptr->flags1 & RF1_NEVER_MOVE) return 0;
    if (r_ptr->flags3 & RF3_NO_FEAR) return 0;
    if (MON_MONFEAR(mon) || MON_CONFUSED(mon)) return 0;  /* the old fear code handles that */
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
    if (hp_pct >= 50 || morale >= 50) return 0;
    return (50 - morale) + (50 - hp_pct);
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
    if (mon_ai_stats.m_idx == mon->id) mon_ai_stats.states[mon->ai_state]++;

    c = _contact(mon);
    mon->ai_contact = c;
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

static void _lose_morale(mon_ptr mon, int amt)
{
    monster_race *r_ptr = &r_info[mon->r_idx];
    if (amt <= 0) return;
    if (r_ptr->flags3 & RF3_NO_FEAR) return;
    if (r_ptr->flags1 & RF1_UNIQUE) amt /= 2;
    mon->morale_lost = MIN(100, mon->morale_lost + amt);
}

static void _interrupt(mon_ptr mon, cptr how)
{
    if (mon->intent != MAI_I_CHARGE) return;
    mon->intent = MAI_I_NONE;
    if (mon_ai_stats.m_idx == mon->id) mon_ai_stats.interrupts++;
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
    if (mon_ai_stats.m_idx == mon->id) mon_ai_stats.charges++;
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
    if (mon_ai_stats.m_idx == mon->id) mon_ai_stats.shouts++;
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
    if (mon_ai_stats.m_idx == mon->id) mon_ai_stats.retreats++;

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
            if (mon_ai_stats.m_idx == mon->id) mon_ai_stats.releases++;
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
