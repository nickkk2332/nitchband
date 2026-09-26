/* File: wizard2.c */

/*
 * Copyright (c) 1997 Ben Harrison, and others
 *
 * This software may be copied and distributed for educational, research,
 * and not for profit purposes provided that this copyright and statement
 * are included in all such copies. Other copyrights may also apply.
 */

/* Purpose: Wizard commands */

#include "angband.h"

#include <assert.h>

/* Statistics: Use the wizard commands '-' and '=' to gather statistics.
   The Wizard command '"' and 'A' will then show all found artifacts,
   including rand-arts. The character sheet will show statistics on object
   distributions and key resources. The object info commands '~' for egos 'e'
   and objects 'o' are also useful. Be sure to begin each statistics run
   with a fresh, newly created character.*/
bool statistics_hack = FALSE;
bool wiz_immortal = FALSE; /* take_hit() never kills (AI harness) */
int  wiz_bonus_to_h = 0, wiz_bonus_to_d = 0, wiz_bonus_dd = 0, wiz_bonus_blows = 0, wiz_bonus_ac = 0, wiz_bonus_speed = 0;
static vec_ptr _rand_arts = NULL;
static vec_ptr _egos = NULL;

vec_ptr stats_rand_arts(void)
{
    if (!_rand_arts)
        _rand_arts = vec_alloc(free);
    return _rand_arts;
}

vec_ptr stats_egos(void)
{
    if (!_egos)
        _egos = vec_alloc(free);
    return _egos;
}

void stats_add_rand_art(object_type *o_ptr)
{
    if (o_ptr->art_name)
    {
        object_type *copy = malloc(sizeof(object_type));
        *copy = *o_ptr;
        no_karrot_hack = TRUE;
        obj_identify_fully(copy);
        no_karrot_hack = FALSE;
        vec_add(stats_rand_arts(), copy);
    }
}

void stats_add_ego(object_type *o_ptr)
{
    if (o_ptr->name2)
    {
        object_type *copy = malloc(sizeof(object_type));
        *copy = *o_ptr;
        obj_identify_fully(copy);
        vec_add(stats_egos(), copy);
    }
}

typedef struct {
    int total;
    int count;
} _tally_t;
static _tally_t _monster_levels[MAX_DEPTH];
static _tally_t _object_levels[MAX_DEPTH];
static int      _object_histogram[MAX_DEPTH];

static void _stats_reset_monster_levels(void)
{
    int i;
    for (i = 0; i < MAX_DEPTH; i++)
    {
        _monster_levels[i].total = 0;
        _monster_levels[i].count = 0;
    }
}

static void _stats_note_monster_level(int dlvl, int mlvl)
{
    if (0 <= dlvl && dlvl < MAX_DEPTH)
    {
        _monster_levels[dlvl].total += mlvl;
        _monster_levels[dlvl].count++;
    }
}

static void _stats_reset_object_levels(void)
{
    int i;
    for (i = 0; i < MAX_DEPTH; i++)
    {
        _object_levels[i].total = 0;
        _object_levels[i].count = 0;

        _object_histogram[i] = 0;
    }
}

static void _stats_note_object_level(int dlvl, int olvl)
{
    if (0 <= dlvl && dlvl < MAX_DEPTH)
    {
        _object_levels[dlvl].total += olvl;
        _object_levels[dlvl].count++;
    }

    if (0 <= olvl && olvl < MAX_DEPTH)
        _object_histogram[olvl]++;
}


/*
 * Strip an "object name" into a buffer
 */
void strip_name_aux(char *dest, const char *src)
{
    char *t;

    /* Skip past leading characters */
    while (*src == ' ' || *src == '&' || *src == '[')
        src++;

    /* Copy useful chars */
    for (t = dest; *src; src++)
    {
        if (*src != '~' && *src != ']')
            *t++ = *src;
    }

    *t = '\0';
}

void strip_name(char *buf, int k_idx)
{
    strip_name_aux(buf, k_name + k_info[k_idx].name);
}

int _life_rating_aux(int lvl)
{
    return (p_ptr->player_hp[lvl-1]-100) * 100 / (50*(lvl-1));
}

int life_rating(void)
{
    return _life_rating_aux(PY_MAX_LEVEL);
}

cptr life_rating_desc(bool use_attr)
{
    int tulos = p_ptr->player_hp[PY_MAX_LEVEL - 1] / 10 - 223;
    char buf[15];
    byte attr;
    if (!use_attr) return (percentage_life ? format("%d/100", life_rating()) : format("%d/76", tulos));
    if (tulos >= 76) /* 76 is the max but let's be paranoid... */
    {
        strcpy(buf, "Optimal");
        attr = TERM_VIOLET;
    }
    else if (tulos >= 66)
    {
        strcpy(buf, "Superb");
        attr = TERM_GREEN; 
    }
    else if (tulos >= 57)
    {
        strcpy(buf, "Excellent");
        attr = TERM_L_GREEN;
    }
    else if (tulos >= 48)
    {
        strcpy(buf, "Very Good");
        attr = TERM_YELLOW;
    }
    else if (tulos >= 39)
    {
        strcpy(buf, "Good");
        attr = TERM_YELLOW;
    }
    else if (tulos >= 29)
    {
        strcpy(buf, "Fair");
        attr = TERM_ORANGE;
    }
    else if (tulos >= 19)
    {
        strcpy(buf, "Bad");
        attr = TERM_L_RED;
    }
    else if (tulos >= 9)
    {
        strcpy(buf, "Very Bad");
        attr = TERM_RED;
    }
    else
    {
        strcpy(buf, "Abysmal");
        attr = TERM_L_DARK;
    }
    if (percentage_life) return format("<color:%c>%s</color> (%d%%)", attr_to_attr_char(attr), buf, life_rating());
    else return format("<color:%c>%s</color> (%d/76)", attr_to_attr_char(attr), buf, tulos);
}

void do_cmd_rerate_aux(void)
{
    for(;;)
    {
        int i, pct;
        p_ptr->player_hp[0] = 100;

        for (i = 1; i < PY_MAX_LEVEL; i++)
            p_ptr->player_hp[i] = p_ptr->player_hp[i - 1] + randint1(100);

        /* These extra early checks give a slight boost to average life ratings (~39) */
        pct = _life_rating_aux(5);
        if (pct < 87) continue;

        pct = _life_rating_aux(10);
        if (pct < 87) continue;

        pct = _life_rating_aux(25);
        if (pct < 87) continue;

        pct = life_rating();
        if (87 <= pct && pct <= 117) break;
    }
}

void do_cmd_rerate(bool display)
{
    do_cmd_rerate_aux();

    p_ptr->update |= (PU_HP);
    p_ptr->redraw |= (PR_HP);
    handle_stuff();

    if (display)
    {
        msg_format("Your life rating is now %s.", life_rating_desc(TRUE));
        p_ptr->knowledge |= KNOW_HPRATE;
    }
    else
    {
        msg_print("Life rate is changed.");
        p_ptr->knowledge &= ~(KNOW_HPRATE);
    }
}


#ifdef ALLOW_WIZARD

/*
 * Dimension Door
 */
static bool wiz_dimension_door(void)
{
    int    x = 0, y = 0;

    if (!tgt_pt(&x, &y, -1)) return FALSE;

    teleport_player_to(y, x, TELEPORT_NONMAGICAL);

    return (TRUE);
}


/*
 * Create the artifact of the specified number -- DAN
 *
 */
static void wiz_create_named_art(int a_idx)
{
    if (create_named_art(a_idx, py, px, ORIGIN_CHEAT, 0))
        a_info[a_idx].generated = TRUE;
}

#ifdef MONSTER_HORDES

/* Summon a horde of monsters */
static void do_cmd_summon_horde(void)
{
    int wy = py, wx = px;
    int attempts = 1000;

    while (--attempts)
    {
        scatter(&wy, &wx, py, px, 3, 0);
        if (cave_empty_bold(wy, wx)) break;
    }

    (void)alloc_horde(wy, wx);
}

#endif /* MONSTER_HORDES */

/*
 * Hack -- Teleport to the target
 */
static void do_cmd_wiz_bamf(void)
{
    /* Must have a target */
    if (!target_who) return;

    /* Teleport to the target */
    teleport_player_to(target_row, target_col, TELEPORT_NONMAGICAL);
}


/*
 * Aux function for "do_cmd_wiz_change()".   -RAK-
 */
static void do_cmd_wiz_change_aux(void)
{
    int i, j;
    int tmp_int;
    long tmp_long;
    s16b tmp_s16b;
    char tmp_val[160];
    char ppp[80];


    /* Query the stats */
    for (i = 0; i < 6; i++)
    {
        /* Prompt */
        sprintf(ppp, "%s (3-%d): ", stat_names[i], p_ptr->stat_max_max[i]);

        /* Default */
        sprintf(tmp_val, "%d", p_ptr->stat_max[i]);

        /* Query */
        if (!get_string(ppp, tmp_val, 4)) return;

        /* Extract */
        tmp_int = atoi(tmp_val);

        /* Verify */
        if (tmp_int > p_ptr->stat_max_max[i]) tmp_int = p_ptr->stat_max_max[i];
        else if (tmp_int < 3) tmp_int = 3;

        /* Save it */
        p_ptr->stat_cur[i] = p_ptr->stat_max[i] = tmp_int;
    }


    /* Default */
    sprintf(tmp_val, "%d", WEAPON_EXP_MASTER);

    /* Query */
    if (!get_string("Proficiency: ", tmp_val, 9)) return;

    /* Extract */
    tmp_s16b = atoi(tmp_val);

    /* Verify */
    if (tmp_s16b < WEAPON_EXP_UNSKILLED) tmp_s16b = WEAPON_EXP_UNSKILLED;
    if (tmp_s16b > WEAPON_EXP_MASTER) tmp_s16b = WEAPON_EXP_MASTER;

    for (j = 0; j <= TV_WEAPON_END - TV_WEAPON_BEGIN; j++)
    {
        for (i = 0;i < 64;i++)
        {
            int max = skills_weapon_max(TV_WEAPON_BEGIN + j, i);
            p_ptr->weapon_exp[j][i] = tmp_s16b;
            if (p_ptr->weapon_exp[j][i] > max) p_ptr->weapon_exp[j][i] = max;
        }
    }

    for (j = 0; j < 10; j++)
    {
        p_ptr->skill_exp[j] = tmp_s16b;
        if (p_ptr->skill_exp[j] > s_info[p_ptr->pclass].s_max[j]) p_ptr->skill_exp[j] = s_info[p_ptr->pclass].s_max[j];
    }

    /* Hack for WARLOCK_DRAGONS. Of course, reading skill tables directly is forbidden, so this code is inherently wrong! */
    p_ptr->skill_exp[SKILL_RIDING] = MIN(skills_riding_max(), tmp_s16b);

    for (j = 0; j < 32; j++)
        p_ptr->spell_exp[j] = (tmp_s16b > SPELL_EXP_MASTER ? SPELL_EXP_MASTER : tmp_s16b);
    for (; j < 64; j++)
        p_ptr->spell_exp[j] = (tmp_s16b > SPELL_EXP_EXPERT ? SPELL_EXP_EXPERT : tmp_s16b);

    /* Default */
    sprintf(tmp_val, "%d", p_ptr->au);

    /* Query */
    if (!get_string("Gold: ", tmp_val, 10)) return;

    /* Extract */
    tmp_long = atol(tmp_val);

    /* Verify */
    if (tmp_long < 0) tmp_long = 0L;

    /* Save */
    p_ptr->au = tmp_long;


    /* Default */
    sprintf(tmp_val, "%d", p_ptr->max_exp);

    /* Query */
    if (!get_string("Experience: ", tmp_val, 10)) return;

    /* Extract */
    tmp_long = atol(tmp_val);

    /* Verify */
    if (tmp_long < 0) tmp_long = 0L;

    if (p_ptr->prace != RACE_ANDROID)
    {
        /* Save */
        p_ptr->max_exp = tmp_long;
        p_ptr->exp = tmp_long;

        /* Update */
        check_experience();
    }

    sprintf(tmp_val, "%d", p_ptr->fame);
    if (!get_string("Fame: ", tmp_val, 4)) return;
    tmp_long = atol(tmp_val);
    if (tmp_long < 0) tmp_long = 0L;
    p_ptr->fame = (s16b)tmp_long;
}


/*
 * Change various "permanent" player variables.
 */
static void do_cmd_wiz_change(void)
{
    /* Interact */
    do_cmd_wiz_change_aux();

    /* Redraw everything */
    do_cmd_redraw();
}

/* Blue-Mage - learn spells for free */
static void do_cmd_wiz_blue_mage(void)
{
    int n = get_quantity("Which type? ", MST_COUNT - 1);
    int e = get_quantity("Which effect? ", 200);
    blue_mage_learn_spell_aux(n, e, 0, 0, TRUE);
}

/*
 * A structure to hold a tval and its description
 */
typedef struct tval_desc
{
    int        tval;
    cptr       desc;
} tval_desc;

/*
 * A list of tvals and their textual names
 */
static tval_desc tvals[] =
{
    { TV_SWORD,             "Sword"                },
    { TV_POLEARM,           "Polearm"              },
    { TV_HAFTED,            "Hafted Weapon"        },
    { TV_BOW,               "Bow"                  },
    { TV_ARROW,             "Arrows"               },
    { TV_BOLT,              "Bolts"                },
    { TV_SHOT,              "Shots"                },
    { TV_SHIELD,            "Shield"               },
    { TV_CROWN,             "Crown"                },
    { TV_HELM,              "Helm"                 },
    { TV_GLOVES,            "Gloves"               },
    { TV_BOOTS,             "Boots"                },
    { TV_CLOAK,             "Cloak"                },
    { TV_DRAG_ARMOR,        "Dragon Scale Mail"    },
    { TV_HARD_ARMOR,        "Hard Armor"           },
    { TV_SOFT_ARMOR,        "Soft Armor"           },
    { TV_RING,              "Ring"                 },
    { TV_AMULET,            "Amulet"               },
    { TV_LITE,              "Lite"                 },
    { TV_POTION,            "Potion"               },
    { TV_SCROLL,            "Scroll"               },
    { TV_WAND,              "Wand"                 },
    { TV_STAFF,             "Staff"                },
    { TV_ROD,               "Rod"                  },
    { TV_LIFE_BOOK,         "Life Spellbook"       },
    { TV_SORCERY_BOOK,      "Sorcery Spellbook"    },
    { TV_NATURE_BOOK,       "Nature Spellbook"     },
    { TV_CHAOS_BOOK,        "Chaos Spellbook"      },
    { TV_DEATH_BOOK,        "Death Spellbook"      },
    { TV_TRUMP_BOOK,        "Trump Spellbook"      },
    { TV_ARCANE_BOOK,       "Arcane Spellbook"     },
    { TV_CRAFT_BOOK,        "Craft Spellbook"},
    { TV_DAEMON_BOOK,       "Daemon Spellbook"},
    { TV_CRUSADE_BOOK,      "Crusade Spellbook"},
    { TV_NECROMANCY_BOOK,   "Necromancy Spellbook"},
    { TV_ARMAGEDDON_BOOK,   "Armageddon Spellbook"},
    { TV_LAW_BOOK,          "Law Spellbook"        },
    { TV_MUSIC_BOOK,        "Music Spellbook"      },
    { TV_HISSATSU_BOOK,     "Book of Kendo"        },
    { TV_HEX_BOOK,          "Hex Spellbook"        },
    { TV_RAGE_BOOK,         "Rage Spellbook"       },
    { TV_BURGLARY_BOOK,     "Thieves' Guide"       },
    { TV_PARCHMENT,         "Parchment" },
    { TV_WHISTLE,           "Whistle"    },
    { TV_QUIVER,            "Quiver"               },
    { TV_SPIKE,             "Spikes"               },
    { TV_DIGGING,           "Digger"               },
    { TV_CHEST,             "Chest"                },
    { TV_CAPTURE,           "Capture Ball"         },
    { TV_CARD,              "Express Card"         },
    { TV_FIGURINE,          "Magical Figurine"     },
    { TV_STATUE,            "Statue"               },
    { TV_CORPSE,            "Corpse"               },
    { TV_FOOD,              "Food"                 },
    { TV_FLASK,             "Flask"                },
    { TV_BOTTLE,            "Bottle"               },
    { TV_JUNK,              "Junk"                 },
    { TV_SKELETON,          "Skeleton"             },
    { 0,                    NULL                   }
};



/*
 * Specify tval and sval (type and subtype of object) originally
 * by RAK, heavily modified by -Bernd-
 *
 * This function returns the k_idx of an object type, or zero if failed
 *
 * List up to 50 choices in three columns
 */
static int wiz_create_itemtype(void)
{
    int i, num, max_num, lvl;
    int col, row;
    int tval;

    cptr tval_desc;
    char ch;

    int choice[120];

    char buf[160];


    /* Clear screen */
    Term_clear();

    /* Print all tval's and their descriptions */
    for (num = 0; (num < 80) && tvals[num].tval; num++)
    {
        row = 2 + (num % 30);
        col = 30 * (num / 30);
        ch = listsym[num];
        prt(format("[%c] %s", ch, tvals[num].desc), row, col);
    }

    /* Me need to know the maximal possible tval_index */
    max_num = num;

    /* Choose! */
    if (!get_com("Get what type of object? ", &ch, FALSE)) return (0);

    /* Analyze choice */
    for (num = 0; num < max_num; num++)
    {
        if (listsym[num] == ch) break;
    }

    /* Bail out if choice is illegal */
    if ((num < 0) || (num >= max_num)) return (0);

    /* Base object type chosen, fill in tval */
    tval = tvals[num].tval;
    tval_desc = tvals[num].desc;


    /*** And now we go for k_idx ***/

    /* Clear screen */
    Term_clear();

    /* We have to search the whole itemlist. */
    num = 0;
    for (lvl = 0; lvl <= 120 && num < 120; lvl++) /* Who cares if this is slow. But order the choices please!! */
    {
        for (i = 1; i < max_k_idx && num < 120; i++)
        {
            object_kind *k_ptr = &k_info[i];

            /* Analyze matching items */
            if (k_ptr->tval == tval && k_ptr->level == lvl)
            {
                /* Prepare it */
                row = 2 + (num % 30);
                col = 30 * (num / 30);
                ch = listsym[num];
                strcpy(buf,"                    ");

                /* Acquire the "name" of object "i" */
                strip_name(buf, i);

                /* Print it */
                if (k_ptr->max_level)
                    prt(format("[%c] %s (L%d-%d)", ch, buf, lvl, k_ptr->max_level), row, col);
                else
                    prt(format("[%c] %s (L%d-*)", ch, buf, lvl), row, col);

                /* Remember the object index */
                choice[num++] = i;
            }
        }
    }

    /* Me need to know the maximal possible remembered object_index */
    max_num = num;

    /* Choose! */
    if (!get_com(format("What Kind of %s? ", tval_desc), &ch, FALSE)) return (0);

    /* Analyze choice */
    for (num = 0; num < max_num; num++)
    {
        if (listsym[num] == ch) break;
    }

    /* Bail out if choice is "illegal" */
    if ((num < 0) || (num >= max_num)) return (0);

    /* And return successful */
    return (choice[num]);
}

/*
 * Wizard routine for creating objects        -RAK-
 * Heavily modified to allow magification and artifactification  -Bernd-
 *
 * Note that wizards cannot create objects on top of other objects.
 *
 * Hack -- this routine always makes a "dungeon object", and applies
 * magic to it, and attempts to decline cursed items.
 */
static void wiz_create_item(void)
{
    object_type    forge;
    object_type *q_ptr;
    int n = 1;

    int k_idx;


    /* Save the screen */
    screen_save();

    /* Get object base type */
    k_idx = wiz_create_itemtype();

    /* Restore the screen */
    screen_load();


    /* Return if failed */
    if (!k_idx) return;

    if (k_info[k_idx].gen_flags & OFG_INSTA_ART)
    {
        int i;

        /* Artifactify */
        for (i = 1; i < max_a_idx; i++)
        {
            /* Ignore incorrect tval */
            if (a_info[i].tval != k_info[k_idx].tval) continue;

            /* Ignore incorrect sval */
            if (a_info[i].sval != k_info[k_idx].sval) continue;

            /* Create this artifact */
            if (create_named_art(i, py, px, ORIGIN_CHEAT, 0))
                a_info[i].generated = TRUE;

            /* All done */
            msg_print("Allocated(INSTA_ART).");

            return;
        }
    }
    else if (k_info[k_idx].tval == TV_CORPSE) /* Possessor Testing! */
    {
        char buf[81];
        buf[0] = 0;
        if (msg_input("Which monster? ", buf, 80))
        {
            n = parse_lookup_monster(buf, 0);
            if (!n) n = atoi(buf);
        }
    }
    else
    {
        switch (k_info[k_idx].tval)
        {
        case TV_WAND: case TV_ROD: case TV_STAFF:
            n = 1;
            break;
        default:
            n = get_quantity("How many? ", 99);
        }
    }

    /* Get local object */
    q_ptr = &forge;

    /* Create the item */
    object_prep(q_ptr, k_idx);

    /* Apply magic */
    apply_magic(q_ptr, dun_level, AM_NO_FIXED_ART);
    if (k_info[k_idx].tval == TV_CORPSE)
    {
        if ((k_info[k_idx].sval >= SV_BODY_HEAD) && (k_info[k_idx].sval < SV_BODY_EARS)) q_ptr->xtra4 = n;
        else q_ptr->pval = n;
    }
    else
        q_ptr->number = n;

    object_origins(q_ptr, ORIGIN_CHEAT);

    /* Drop the object from heaven */
    (void)drop_near(q_ptr, -1, py, px);

    /* All done */
    msg_print("Allocated.");
}


/*
 * Cure everything instantly
 */
static void do_cmd_wiz_cure_all(void)
{
    /* Restore stats */
    (void)res_stat(A_STR);
    (void)res_stat(A_INT);
    (void)res_stat(A_WIS);
    (void)res_stat(A_CON);
    (void)res_stat(A_DEX);
    (void)res_stat(A_CHR);

    /* Restore the level */
    (void)restore_level();

    /* Heal the player */
    if (p_ptr->chp < p_ptr->mhp)
    {
        p_ptr->chp = p_ptr->mhp;
        p_ptr->chp_frac = 0;

        /* Redraw */
        p_ptr->redraw |= (PR_HP);
    }

    /* Restore mana */
    if (p_ptr->csp < p_ptr->msp)
    {
        if (!elemental_is_(ELEMENTAL_WATER)) p_ptr->csp = p_ptr->msp;
        p_ptr->csp_frac = 0;

        p_ptr->redraw |= (PR_MANA);
        p_ptr->window |= (PW_SPELL);
    }

    /* Cure stuff */
    (void)set_blind(0, TRUE);
    (void)set_confused(0, TRUE);
    (void)set_poisoned(0, TRUE);
    fear_clear_p();
    (void)set_paralyzed(0, TRUE);
    (void)set_image(0, TRUE);
    (void)set_stun(0, TRUE);
    (void)set_cut(0, TRUE);
    (void)set_slow(0, TRUE);
    (void)p_inc_minislow(-10);
    (void)set_unwell(0, TRUE);

    /* No longer hungry
    (void)set_food(PY_FOOD_MAX - 1);*/
}


/*
 * Go to any level
 */
static void do_cmd_wiz_jump(void)
{
    /* Ask for level */
    if (command_arg <= 0)
    {
        char    ppp[80];

        char    tmp_val[160];
        int        tmp_dungeon_type;

        /* Prompt */
        sprintf(ppp, "Jump which dungeon : ");

        /* Default */
        sprintf(tmp_val, "%d", dungeon_type);

        /* Ask for a level */
        if (!get_string(ppp, tmp_val, 4)) return;

        tmp_dungeon_type = atoi(tmp_val);
        if (!d_info[tmp_dungeon_type].maxdepth || (tmp_dungeon_type > max_d_idx)) tmp_dungeon_type = DUNGEON_ANGBAND;

        /* Prompt */
        sprintf(ppp, "Jump to level (0, %d-%d): ", d_info[tmp_dungeon_type].mindepth, d_info[tmp_dungeon_type].maxdepth);

        /* Default */
        sprintf(tmp_val, "%d", dun_level);

        /* Ask for a level */
        if (!get_string(ppp, tmp_val, 10)) return;

        /* Extract request */
        command_arg = atoi(tmp_val);

        set_dungeon_type(tmp_dungeon_type);
    }

    /* Paranoia */
    if (command_arg < d_info[dungeon_type].mindepth) command_arg = 0;

    /* Paranoia */
    if (command_arg > d_info[dungeon_type].maxdepth) command_arg = d_info[dungeon_type].maxdepth;

    /* Accept request */
    msg_format("<color:U>You jump to dungeon level %d:</color>", command_arg);

    if (autosave_l) do_cmd_save_game(TRUE);

    /* Change level */
    dun_level = command_arg;

    prepare_change_floor_mode(CFM_RAND_PLACE);

    if (!dun_level) set_dungeon_type(0);
    p_ptr->inside_arena = FALSE;
    p_ptr->wild_mode = FALSE;

    quests_on_leave();
    energy_use = 0;

    /* Prevent energy_need from being too lower than 0 */
    p_ptr->energy_need = 0;

    /*
     * Clear all saved floors
     * and create a first saved floor
     */
    prepare_change_floor_mode(CFM_FIRST_FLOOR);

    /* Leaving */
    p_ptr->leaving = TRUE;
}


/*
 * Become aware of a lot of objects
 */
static void do_cmd_wiz_learn(void)
{
    int i;

    object_type forge;
    object_type *q_ptr;

    /* Scan every object */
    for (i = 1; i < max_k_idx; i++)
    {
        object_kind *k_ptr = &k_info[i];

        /* Induce awareness */
        if (k_ptr->level <= command_arg)
        {
            /* Get local object */
            q_ptr = &forge;

            /* Prepare object */
            object_prep(q_ptr, i);

            /* Awareness */
            object_aware(q_ptr);
        }
    }
}


/*
 * Summon some creatures
 */
static void do_cmd_wiz_summon(int num)
{
    int i;

    for (i = 0; i < num; i++)
    {
        (void)summon_specific(0, py, px, dun_level, 0, (PM_ALLOW_GROUP | PM_ALLOW_UNIQUE));
    }
}


/*
 * Summon a creature of the specified type
 *
 * XXX XXX XXX This function is rather dangerous
 */
static void do_cmd_wiz_named(int r_idx)
{
    int x = px;
    int y = py;

    if (target_who < 0)
    {
        x = target_col;
        y = target_row;
    }

    {
        monster_race *r_ptr = &r_info[r_idx];
        if (((r_ptr->flags1 & (RF1_UNIQUE)) ||
                (r_ptr->flags7 & (RF7_NAZGUL))) &&
            (r_ptr->cur_num >= mon_available_num(r_ptr)))
        {
            r_ptr->max_num = MAX(r_ptr->max_num, 1);
            /* In the event that the summon still fails, let it fail
             * We resurrect dead uniques but not captured uniques and do
             * not create multiple copies of the unique */
        }
//        msg_format("Max: %d Cur: %d Ball: %d", r_ptr->max_num, r_ptr->cur_num, r_ptr->ball_num);
    }


    (void)summon_named_creature(0, y, x, r_idx, (PM_ALLOW_SLEEP | PM_ALLOW_GROUP));
}


/*
 * Summon a creature of the specified type
 *
 * XXX XXX XXX This function is rather dangerous
 */
static void do_cmd_wiz_named_friendly(int r_idx)
{
    (void)summon_named_creature(0, py, px, r_idx, (PM_ALLOW_SLEEP | PM_ALLOW_GROUP | PM_FORCE_PET));
}



/*
 * Hack -- Delete all nearby monsters
 */
static void do_cmd_wiz_zap(void)
{
    int i;


    /* Genocide everyone nearby */
    for (i = 1; i < m_max; i++)
    {
        monster_type *m_ptr = &m_list[i];

        /* Paranoia -- Skip dead monsters */
        if (!m_ptr->r_idx) continue;

        /* Skip the mount */
        if (i == p_ptr->riding) continue;

        /* Delete nearby monsters */
        if (m_ptr->cdis <= MAX_SIGHT)
        {
            bool fear = FALSE;
            mon_take_hit(i, m_ptr->hp + 1, DAM_TYPE_WIZARD, &fear, NULL);
            /*delete_monster_idx(i);*/
        }
    }
}


/*
 * Hack -- Delete all monsters
 */
static void do_cmd_wiz_zap_all(void)
{
    int i;

    /* Genocide everyone */
    for (i = 1; i < m_max; i++)
    {
        monster_type *m_ptr = &m_list[i];

        /* Paranoia -- Skip dead monsters */
        if (!m_ptr->r_idx) continue;

        /* Skip the mount */
        if (i == p_ptr->riding) continue;

        delete_monster_idx(i);
    }
}


/*
 * Create desired feature
 */
static void do_cmd_wiz_create_feature(void)
{
    static int   prev_feat = 0;
    static int   prev_mimic = 0;
    cave_type    *c_ptr;
    feature_type *f_ptr;
    char         tmp_val[160];
    int          tmp_feat, tmp_mimic;
    int          y, x;

    if (!tgt_pt(&x, &y, -1)) return;

    c_ptr = &cave[y][x];

    /* Default */
    sprintf(tmp_val, "%d", prev_feat);

    /* Query */
    if (!get_string("Feature: ", tmp_val, 4)) return;

    /* Extract */
    tmp_feat = atoi(tmp_val);
    if (tmp_feat < 0) tmp_feat = 0;
    else if (tmp_feat >= max_f_idx) tmp_feat = max_f_idx - 1;

    /* Default */
    sprintf(tmp_val, "%d", prev_mimic);

    /* Query */
    if (!get_string("Feature (mimic): ", tmp_val, 4)) return;

    /* Extract */
    tmp_mimic = atoi(tmp_val);
    if (tmp_mimic < 0) tmp_mimic = 0;
    else if (tmp_mimic >= max_f_idx) tmp_mimic = max_f_idx - 1;

    cave_set_feat(y, x, tmp_feat);
    c_ptr->mimic = tmp_mimic;

    f_ptr = &f_info[get_feat_mimic(c_ptr)];

    if (have_flag(f_ptr->flags, FF_GLYPH) ||
        have_flag(f_ptr->flags, FF_MON_TRAP))
        c_ptr->info |= (CAVE_OBJECT);
    else if (have_flag(f_ptr->flags, FF_MIRROR))
        c_ptr->info |= (CAVE_GLOW | CAVE_OBJECT);

    /* Notice */
    note_spot(y, x);

    /* Redraw */
    lite_spot(y, x);

    /* Update some things */
    p_ptr->update |= (PU_FLOW);

    prev_feat = tmp_feat;
    prev_mimic = tmp_mimic;
}

/*************************************************************************
 * Wizard Stats
 ************************************************************************/
static doc_ptr _wiz_doc = NULL;
static bool    _wiz_show_scores = TRUE;
static int     _wiz_obj_count = 0;
static int     _wiz_obj_score = 0;

static void _wiz_stats_begin(void)
{
    _wiz_doc = doc_alloc(120);
    doc_insert(_wiz_doc, "<style:table>");
    _wiz_obj_count = 0;
    _wiz_obj_score = 0;
    statistics_hack = TRUE;
}

static void _wiz_stats_end(void)
{
    doc_insert(_wiz_doc, "</style>");
    if (_wiz_obj_count)
    {
        doc_printf(_wiz_doc, "\n\n<color:R>%d</color> objects. <color:R>%d</color> average score.\n",
            _wiz_obj_count, _wiz_obj_score / _wiz_obj_count);
    }
    if (original_score)
        doc_printf(_wiz_doc, "<color:R>%d%%</color> replacement power.\n", replacement_score * 100 / original_score);
    statistics_hack = FALSE;
}

static void _wiz_stats_free(void)
{
    doc_free(_wiz_doc);
    _wiz_doc = NULL;
}

static void _wiz_stats_display(void)
{
    if (doc_line_count(_wiz_doc))
        doc_display(_wiz_doc, "Statistics", 0);

    viewport_verify();
    do_cmd_redraw();
}

static char _score_color(int score)
{
    if (score < 1000)
        return 'D';
    if (score < 10000)
        return 'w';
    if (score < 20000)
        return 'W';
    if (score < 40000)
        return 'u';
    if (score < 60000)
        return 'y';
    if (score < 80000)
        return 'o';
    if (score < 100000)
        return 'R';
    if (score < 150000)
        return 'r';
    return 'v';
}

#if 0
static void _wiz_stats_log_android(int level, object_type *o_ptr)
{
    int  score = obj_value_real(o_ptr);
    int  exp   = android_obj_exp(o_ptr);
    char name[MAX_NLEN];
    char buf[10];

    if (!_wiz_doc) return;
    if (!exp) return;

    object_desc(name, o_ptr, OD_COLOR_CODED);

    big_num_display(score, buf);
    doc_printf(_wiz_doc, "<color:%c>%s</color> ", _score_color(score), buf);

    big_num_display(exp, buf);
    doc_printf(_wiz_doc, "<color:%c>%s</color>:", _score_color(exp/10), buf);

    doc_printf(_wiz_doc, " <indent><style:indent>%s</style></indent>\n", name);
}
#endif

static void _wiz_stats_log_device(int level, object_type *o_ptr)
{
    char buf[MAX_NLEN];
    if (!_wiz_doc) return;
    object_desc(buf, o_ptr, OD_COLOR_CODED);
    _wiz_obj_count++;
    doc_printf(_wiz_doc, "C%2d D%2d O%2d P%2d D%2d: <indent><style:indent>%s</style></indent>\n",
        p_ptr->lev, level, o_ptr->level, o_ptr->activation.power, o_ptr->activation.difficulty, buf);
}
static void _wiz_stats_log_obj(int level, object_type *o_ptr)
{
    char buf[MAX_NLEN];
    if (!_wiz_doc) return;
    object_desc(buf, o_ptr, OD_COLOR_CODED);
    _wiz_obj_count++;
    if (_wiz_show_scores)
    {
        int  score;
        score = obj_value_real(o_ptr);
        _wiz_obj_score += score;
        doc_printf(_wiz_doc, "C%2d D%2d O%2d <color:%c>%6d</color>: <indent><style:indent>%s</style></indent>\n",
            p_ptr->lev, level, o_ptr->level, _score_color(score), score, buf);
    }
    else
        doc_printf(_wiz_doc, "C%2d D%2d O%2d: <indent><style:indent>%s</style></indent>\n", p_ptr->lev, level, o_ptr->level, buf);
}
static void _wiz_stats_log_speed(int level, object_type *o_ptr)
{
    u32b flgs[OF_ARRAY_SIZE];
    obj_flags(o_ptr, flgs);
    if (have_flag(flgs, OF_SPEED) && !object_is_artifact(o_ptr))
        _wiz_stats_log_obj(level, o_ptr);
}
static void _wiz_stats_log_books(int level, object_type *o_ptr, int max3, int max4)
{
    if (obj_is_book(o_ptr) && o_ptr->tval != TV_ARCANE_BOOK)
    {
            if ( (o_ptr->sval == 2 && k_info[o_ptr->k_idx].counts.found < max3)
              || (o_ptr->sval == 3 && k_info[o_ptr->k_idx].counts.found < max4) )
        {
            if (check_book_realm(o_ptr->tval, o_ptr->sval))
                _wiz_stats_log_obj(level, o_ptr);
        }
    }
}
static void _wiz_stats_log_devices(int level, object_type *o_ptr)
{
    #if 0
    if (o_ptr->tval == TV_WAND)
    {
        switch (o_ptr->activation.type)
        {
        case EFFECT_BALL_DISINTEGRATE:
        case EFFECT_BALL_WATER:
        case EFFECT_ROCKET:
            _wiz_stats_log_device(level, o_ptr);
            break;
        }
    }
    #endif
    #if 0
    if (o_ptr->tval == TV_STAFF)
    {
        switch (o_ptr->activation.type)
        {
        case EFFECT_HEAL_CURING:
            _wiz_stats_log_device(level, o_ptr);
            break;
        }
    }
    #endif

    #if 1
    if (obj_is_device(o_ptr))
        _wiz_stats_log_device(level, o_ptr);
    #endif
}
static void _wiz_stats_log_arts(int level, object_type *o_ptr)
{
    if (o_ptr->name1)
        _wiz_stats_log_obj(level, o_ptr);
}
static void _wiz_stats_log_rand_arts(int level, object_type *o_ptr)
{
    if (o_ptr->art_name)
        _wiz_stats_log_obj(level, o_ptr);
}
static bool _wiz_stats_skip(point_t pt)
{
    if (0 && (cave[pt.y][pt.x].info & CAVE_ICKY)) return TRUE;
    return FALSE;
}
static void _wiz_stats_kill(int level)
{
    int i;

    for (i = 1; i < m_max; i++)
    {
        monster_type *m_ptr = &m_list[i];
        monster_race *r_ptr;
        bool          fear = FALSE;
        int           slot = equip_find_obj(TV_SWORD, SV_RUNESWORD);

        if (!m_ptr->r_idx) continue;
        if (i == p_ptr->riding) continue;

        r_ptr = &r_info[m_ptr->r_idx];
        if (0 && r_ptr->level > level) continue;
        if (r_ptr->id == MON_DAWN) continue; /* inflates pct of humans */
        if (0 && (r_ptr->flags1 & RF1_UNIQUE) && r_ptr->level > level - 4) continue;
        if (_wiz_stats_skip(point(m_ptr->fx, m_ptr->fy))) continue;

        r_ptr->r_sights++;
        _stats_note_monster_level(level, r_ptr->level);
        mon_take_hit(i, m_ptr->hp + 1, DAM_TYPE_WIZARD, &fear, NULL);
        if (slot) rune_sword_kill(equip_obj(slot), r_ptr);
    }
}
static bool _is_stat_potion(obj_ptr obj)
{
    if (obj->tval != TV_POTION) return FALSE;
    switch (obj->sval)
    {
    case SV_POTION_INC_STR:
    case SV_POTION_INC_INT:
    case SV_POTION_INC_WIS:
    case SV_POTION_INC_DEX:
    case SV_POTION_INC_CON:
    case SV_POTION_INC_CHR: return TRUE;
    }
    return FALSE;
}

static bool _wiz_improve_gear_aux(obj_ptr obj, slot_t slot)
{
    obj_ptr old = equip_obj(slot);
    if (old)
    {
        int score, old_score;
        if (object_is_melee_weapon(old) && !object_is_melee_weapon(obj)) return FALSE;
        if (object_is_shield(old) && !object_is_shield(obj)) return FALSE;
        score = obj_value_real(obj);
        old_score = obj_value_real(old);
        if (score > old_score)
        {
            home_carry(old);
            equip_remove(slot);
            equip_wield(obj, slot);
            return TRUE;
        }
    }
    else
    {
        equip_wield(obj, slot);
        return TRUE;
    }
    return FALSE;
}

static void _wiz_improve_gear(obj_ptr obj)
{
    slot_t slot;

    /* XXX this is tedious ... */
    if (object_is_gloves(obj))
    {
        class_t *class_ptr = get_class();
        caster_info *caster = NULL;

        if (class_ptr->caster_info)
            caster = class_ptr->caster_info();

        if (caster && caster->options & CASTER_GLOVE_ENCUMBRANCE)
        {
            u32b flags[OF_ARRAY_SIZE];
            obj_flags(obj, flags);
            if ( !have_flag(flags, OF_FREE_ACT)
              && !have_flag(flags, OF_DEX)
              && !have_flag(flags, OF_MAGIC_MASTERY) )
            {
                return;
            }
        }
    }
    if (object_is_melee_weapon(obj) && skills_weapon_is_icky(obj->tval, obj->sval)) return;

    if (obj->name1 == ART_POWER || obj->name1 == ART_STONEMASK || have_flag(obj->flags, OF_NO_SUMMON)) return;
    if (obj->name2 == EGO_RING_NAZGUL) return;
    /* hydras have many heads ... */
    for (slot = equip_first_slot(obj); slot; slot = equip_next_slot(obj, slot))
    {
        if (_wiz_improve_gear_aux(obj, slot))
            break;
    }
}

static obj_ptr _pack_obj = NULL;
static bool _improve_pack_p(obj_ptr obj)
{
    assert(_pack_obj);
    return obj->tval == _pack_obj->tval && obj->sval == _pack_obj->sval;
}

static void _wiz_improve_pack(obj_ptr obj)
{
    if (obj->tval == TV_POTION || obj->tval == TV_SCROLL)
    {
        int ct;
        _pack_obj = obj;
        ct = pack_count(_improve_pack_p);
        if (ct + obj->number < 30)
            pack_carry_aux(obj);
        else
            home_carry(obj);
    }
    else if (obj_is_device(obj))
    {
        int slot = pack_find_device(obj->activation.type);

        if (!slot)
            pack_carry_aux(obj);
        else
        {
            obj_ptr old = pack_obj(slot);
            int score, old_score;
            score = obj_value_real(obj);
            old_score = obj_value_real(old);
            if (score > old_score)
            {
                home_carry(old);
                pack_remove(slot);
                pack_carry_aux(obj);
            }
        }
    }
    else if (obj_is_readable_book(obj))
    {
        int ct;
        _pack_obj = obj;
        ct = pack_count(_improve_pack_p);
        if (ct < 2)
            pack_carry_aux(obj);
    }
    else
    {
        pack_carry_aux(obj);
    }
}

static bool _device_is_(obj_ptr obj, int tval, int effect)
{
    return obj->tval == tval
        && obj->activation.type == effect;
}

static void _wiz_stats_inspect(int level)
{
    race_t  *race_ptr = get_race();
    class_t *class_ptr = get_class();
    int      i;

    no_karrot_hack = TRUE;

    for (i = 0; i < max_o_idx; i++)
    {
        object_type *o_ptr = &o_list[i];

        if (!o_ptr->k_idx) continue;
        if (o_ptr->tval == TV_GOLD)
        {
            pack_get(o_ptr);
            continue;
        }
        if (o_ptr->held_m_idx) continue;
        if (o_ptr->marked & OM_COUNTED) continue; /* skip player drops */
        if (_wiz_stats_skip(point(o_ptr->loc.x, o_ptr->loc.y))) continue;

        obj_identify_fully(o_ptr);
        stats_on_identify(o_ptr);
        if (o_ptr->level)
            _stats_note_object_level(level, o_ptr->level);

        if (o_ptr->art_name)
            stats_add_rand_art(o_ptr);

        if (o_ptr->name2)
            stats_add_ego(o_ptr);

        /* Logging: I simply hand-edit this and recompile as desired */
        if (0 && (o_ptr->name1 || o_ptr->name3))
            _wiz_stats_log_obj(level, o_ptr);

        if (0) _wiz_stats_log_speed(level, o_ptr);
        if (0) _wiz_stats_log_books(level, o_ptr, 20, 20);
        if (1) _wiz_stats_log_devices(level, o_ptr);
        if (0) _wiz_stats_log_arts(level, o_ptr);
        if (0) _wiz_stats_log_rand_arts(level, o_ptr);

        if (0 && o_ptr->name3)
            _wiz_stats_log_obj(level, o_ptr);

        if (0 && o_ptr->name2 && !object_is_device(o_ptr) && !object_is_ammo(o_ptr))
            _wiz_stats_log_obj(level, o_ptr);

        if (0 && !object_is_nameless(o_ptr) && o_ptr->tval == TV_BOW)
            _wiz_stats_log_obj(level, o_ptr);

        if (0 && !object_is_nameless(o_ptr) && o_ptr->tval == TV_QUIVER)
            _wiz_stats_log_obj(level, o_ptr);

        if (0 && !object_is_nameless(o_ptr) && object_is_ammo(o_ptr))
            _wiz_stats_log_obj(level, o_ptr);

        if (0 && object_is_dragon_armor(o_ptr))
            _wiz_stats_log_obj(level, o_ptr);

        if (0 && _device_is_(o_ptr, TV_WAND, EFFECT_BALL_WATER))
            _wiz_stats_log_obj(level, o_ptr);

        /* Use Resources: Quaff stat potions and improve equipment (mindlessly).
         * This makes it easier for me to poke around a bit after a stat run.
         * Destroying objects is for Death-swords or other race/classes that
         * gain powers that way. */
        if (_is_stat_potion(o_ptr))
            do_device(o_ptr, SPELL_CAST, 0);

        /* Use the autopicker to 'improve' this character. For example, you can
         * conditionally have rogues only use weapons less than a certain weight
         * and only shoot slings. */
        if (o_ptr->number)
        {
            int auto_pick_idx = is_autopick(o_ptr);
            if (auto_pick_idx >= 0 && autopick_list[auto_pick_idx].action & DO_AUTOPICK)
            {
                if (object_is_wearable(o_ptr))
                    _wiz_improve_gear(o_ptr);
                else
                    _wiz_improve_pack(o_ptr);
            }
            else if ( (auto_pick_idx < 0 || !(autopick_list[auto_pick_idx].action & DO_AUTODESTROY))
                   && object_is_wearable(o_ptr) )
            {
                _wiz_improve_gear(o_ptr);
            }
        }

        if (o_ptr->number)
        {
            if (race_ptr->destroy_object)
                race_ptr->destroy_object(o_ptr);

            if (class_ptr->destroy_object)
                class_ptr->destroy_object(o_ptr);
        }
    }
    pack_overflow();
    home_optimize();
    if (p_ptr->cursed) remove_all_curse();
    no_karrot_hack = FALSE;
}
static void _wiz_stats_gather(int which_dungeon, int level, int reps)
{
    int i;
    set_dungeon_type(which_dungeon);
    for (i = 0; i < reps; i++)
    {
        quests_on_leave();

        dun_level = level;
        prepare_change_floor_mode(CFM_RAND_PLACE);
        energy_use = 0;
        p_ptr->energy_need = 0;
        change_floor();

        _wiz_stats_kill(level);
        _wiz_stats_inspect(level);
    }
}

/*************************************************************************
 * Handle the ^A wizard commands. Perhaps there should be a UI for this?
 ************************************************************************/
/*************************************************************************
 * AI Duel Simulator
 *
 * Runs repeated one-on-one fights between two monster races next to the
 * player, arena style (both sides "friendly", inside_battle set so they
 * fight each other and leave the player alone), with messages muted.
 * Useful for checking that AI changes do not shift balance unexpectedly.
 * Monster-vs-monster only: it does not exercise the spell AI against the
 * player. Monsters do not regenerate during a fight (that happens in
 * process_world, which is not run).
 ************************************************************************/
static int _wiz_prompt_race(cptr prompt)
{
    char buf[81], *s;
    int  idx;
    buf[0] = 0;
    if (!msg_input(prompt, buf, 80)) return 0;
    for (s = buf; *s; s++) *s = tolower((unsigned char)*s); /* lookup is lower case */
    idx = parse_lookup_monster(buf, 0);
    if (!idx) idx = atoi(buf);
    if (idx <= 0 || idx >= max_r_idx || !r_info[idx].name) return 0;
    return idx;
}

static bool _wiz_duel_spots(point_t *a, point_t *b)
{
    int tries;
    for (tries = 0; tries < 5000; tries++)
    {
        int y1 = py + randint0(13) - 6, x1 = px + randint0(13) - 6;
        int y2 = y1 + randint0(13) - 6, x2 = x1 + randint0(13) - 6;
        int d;
        if (!in_bounds(y1, x1) || !in_bounds(y2, x2)) continue;
        if (!cave_empty_bold(y1, x1) || !cave_empty_bold(y2, x2)) continue;
        if (player_bold(y1, x1) || player_bold(y2, x2)) continue;
        d = distance(y1, x1, y2, x2);
        if (d < 3 || d > 6) continue;
        if (distance(py, px, y1, x1) > 6 || distance(py, px, y2, x2) > 6) continue;
        if (!projectable(y1, x1, y2, x2)) continue;
        *a = point(x1, y1);
        *b = point(x2, y2);
        return TRUE;
    }
    return FALSE;
}

static int _wiz_duel_place(int r_idx, point_t pt, u16b tag)
{
    int m_idx;
    if (!place_monster_aux(0, pt.y, pt.x, r_idx, PM_NO_KAGE | PM_NO_PET)) return 0;
    m_idx = cave[pt.y][pt.x].m_idx;
    if (!m_idx) return 0;
    set_friendly(&m_list[m_idx]);
    (void)set_monster_csleep(m_idx, 0);
    m_list[m_idx].mflag &= ~MFLAG_NICE;
    m_list[m_idx].nickname = tag; /* survives only while this monster does */
    return m_idx;
}

static bool _wiz_duel_alive(int m_idx, u16b tag)
{
    return m_idx && m_list[m_idx].r_idx && m_list[m_idx].nickname == tag;
}

/* FNV-1a over the RNG state: two builds that consume random numbers in
 * exactly the same way end a seeded run with the same fingerprint. */
static u32b _wiz_rng_fingerprint(void)
{
    u32b h = 2166136261U;
    int  i;
    for (i = 0; i < RAND_DEG; i++)
    {
        h ^= Rand_state[i];
        h *= 16777619U;
    }
    h ^= Rand_place;
    h *= 16777619U;
    return h;
}

static void _wiz_ai_duel(void)
{
    int     r_a, r_b, trials, i, t;
    int     wins_a = 0, wins_b = 0, draws = 0, fails = 0;
    s32b    turns_total = 0, hp_a = 0, hp_b = 0;
    point_t spot_a, spot_b;
    char    buf[81];
    const int max_turns = 5000; /* game turns (about 500 player turns at normal speed) */
    u16b    tag_a = quark_add("<duel A>"), tag_b = quark_add("<duel B>");
    s32b    old_game_turn = game_turn;
    bool    old_battle = p_ptr->inside_battle;
    int     old_invuln = p_ptr->invuln;
    int     old_chp = p_ptr->chp;
    byte    old_max_a, old_max_b;
    u32b    seed = 0, fingerprint = 0;
    bool    old_rand_quick = FALSE;
    u32b    old_rand_value = 0;
    u16b    old_rand_place = 0;
    u32b    old_rand_state[RAND_DEG];
    doc_ptr doc;

    if (p_ptr->inside_arena || p_ptr->inside_battle || p_ptr->wild_mode)
    {
        msg_print("Not here.");
        return;
    }
    r_a = _wiz_prompt_race("First monster? ");
    if (!r_a) return;
    r_b = _wiz_prompt_race("Second monster? ");
    if (!r_b) return;
    strcpy(buf, "100");
    if (!msg_input("Number of fights? ", buf, 10)) return;
    trials = atoi(buf);
    if (trials < 1) return;
    if (trials > 10000) trials = 10000;
    strcpy(buf, "0");
    if (!msg_input("Random seed (0 = don't fix)? ", buf, 12)) return;
    seed = strtoul(buf, NULL, 10);
    if (!get_check("This deletes every monster on the level. Continue? ")) return;

    if (seed)
    {
        /* Save the game's RNG and run the whole experiment (including the
         * choice of fighting spots) from a fixed state */
        old_rand_quick = Rand_quick;
        old_rand_value = Rand_value;
        old_rand_place = Rand_place;
        C_COPY(old_rand_state, Rand_state, RAND_DEG, u32b);
        Rand_quick = FALSE;
        Rand_state_init(seed);
    }
    if (!_wiz_duel_spots(&spot_a, &spot_b))
    {
        if (seed)
        {
            Rand_quick = old_rand_quick;
            Rand_value = old_rand_value;
            Rand_place = old_rand_place;
            C_COPY(Rand_state, old_rand_state, RAND_DEG, u32b);
        }
        msg_print("Could not find room for the fight near you. Try a more open area.");
        return;
    }

    old_max_a = r_info[r_a].max_num;
    old_max_b = r_info[r_b].max_num;
    statistics_hack = TRUE;
    p_ptr->invuln = 1000;

    for (i = 0; i < trials; i++)
    {
        int a, b;
        bool alive_a, alive_b;

        do_cmd_wiz_zap_all();
        p_ptr->inside_battle = TRUE;
        a = _wiz_duel_place(r_a, spot_a, tag_a);
        b = _wiz_duel_place(r_b, spot_b, tag_b);
        if (!a || !b)
        {
            fails++;
            p_ptr->inside_battle = old_battle;
            continue;
        }

        for (t = 0; t < max_turns; t++)
        {
            game_turn++;
            process_monsters();
            alive_a = _wiz_duel_alive(a, tag_a);
            alive_b = _wiz_duel_alive(b, tag_b);
            if (!alive_a || !alive_b) break;
            if (p_ptr->leaving || p_ptr->is_dead) break;
        }
        alive_a = _wiz_duel_alive(a, tag_a);
        alive_b = _wiz_duel_alive(b, tag_b);
        turns_total += t;
        if (alive_a && !alive_b)
        {
            wins_a++;
            hp_a += m_list[a].hp * 100 / MAX(1, m_list[a].maxhp);
        }
        else if (alive_b && !alive_a)
        {
            wins_b++;
            hp_b += m_list[b].hp * 100 / MAX(1, m_list[b].maxhp);
        }
        else
            draws++;

        p_ptr->inside_battle = old_battle;
        if (p_ptr->leaving || p_ptr->is_dead) break;
    }

    do_cmd_wiz_zap_all();
    p_ptr->inside_battle = old_battle;
    game_turn = old_game_turn;
    p_ptr->invuln = old_invuln;
    if (p_ptr->chp < old_chp) p_ptr->chp = old_chp;
    r_info[r_a].max_num = old_max_a;
    r_info[r_b].max_num = old_max_b;
    if (seed)
    {
        fingerprint = _wiz_rng_fingerprint();
        Rand_quick = old_rand_quick;
        Rand_value = old_rand_value;
        Rand_place = old_rand_place;
        C_COPY(Rand_state, old_rand_state, RAND_DEG, u32b);
    }
    statistics_hack = FALSE;
    p_ptr->update |= PU_MONSTERS | PU_BONUS;
    p_ptr->redraw |= PR_MAP | PR_HP;
    p_ptr->window |= PW_MONSTER_LIST;

    doc = doc_alloc(80);
    doc_printf(doc, "<color:G>AI Duel:</color> <color:y>%s</color> vs <color:y>%s</color>\n\n", r_name + r_info[r_a].name, r_name + r_info[r_b].name);
    i = wins_a + wins_b + draws;
    doc_printf(doc, "Fights run: %d", i);
    if (fails) doc_printf(doc, " (%d could not be set up)", fails);
    doc_newline(doc);
    if (i)
    {
        doc_printf(doc, "%-30.30s wins: <color:R>%3d%%</color>", r_name + r_info[r_a].name, wins_a * 100 / i);
        if (wins_a) doc_printf(doc, "  (avg %d%% HP left)", hp_a / wins_a);
        doc_newline(doc);
        doc_printf(doc, "%-30.30s wins: <color:R>%3d%%</color>", r_name + r_info[r_b].name, wins_b * 100 / i);
        if (wins_b) doc_printf(doc, "  (avg %d%% HP left)", hp_b / wins_b);
        doc_newline(doc);
        doc_printf(doc, "%-30.30s     : <color:R>%3d%%</color>\n", "Draws (time limit)", draws * 100 / i);
        doc_printf(doc, "Average length: %d game turns (limit %d)\n", turns_total / i, max_turns);
    }
    if (seed)
        doc_printf(doc, "Seed %lu, RNG fingerprint <color:B>%08lX</color>\n", (unsigned long)seed, (unsigned long)fingerprint);
    doc_insert(doc, "\n<color:D>Monster-vs-monster only; no regeneration during fights.</color>\n");
    doc_display(doc, "AI Duel", 0);
    doc_free(doc);
    do_cmd_redraw();
}

/*************************************************************************
 * AI Kite Test
 *
 * Measures how one hostile monster behaves against the player: it starts
 * next to you, and you either stand still or chase it (one step toward it
 * per player turn at your real speed; you never attack). The player cannot
 * die during the test (HP is topped up), but other effects of the monster's
 * attacks are real, so use a throwaway character.
 ************************************************************************/
static void _wiz_player_chase_step(int m_idx)
{
    monster_type *m_ptr = &m_list[m_idx];
    int d, best_d = -1, best_dist = distance(py, px, m_ptr->fy, m_ptr->fx);

    for (d = 0; d < 8; d++)
    {
        int y = py + ddy_ddd[d], x = px + ddx_ddd[d];
        int dist;
        if (!in_bounds(y, x)) continue;
        if (!cave_empty_bold(y, x)) continue;
        if (!cave_have_flag_bold(y, x, FF_MOVE)) continue;
        dist = distance(y, x, m_ptr->fy, m_ptr->fx);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_d = d;
        }
    }
    if (best_d >= 0)
        move_player_effect(py + ddy_ddd[best_d], px + ddx_ddd[best_d], MPE_DONT_PICKUP | MPE_HANDLE_STUFF);
}

/* Group mode: move the player to the nearest square in the open (all 8
 * neighbours walkable, and theirs too) so squad tactics are tested in a
 * room rather than against a building */
static void _wiz_kite_open_ground(void)
{
    int y, x, best_d = 999, by = 0, bx = 0;
    for (y = MAX(2, py - 25); y <= MIN(cur_hgt - 3, py + 25); y++)
    {
        for (x = MAX(2, px - 25); x <= MIN(cur_wid - 3, px + 25); x++)
        {
            int yy, xx, d = distance(py, px, y, x);
            bool open = TRUE;
            if (d >= best_d) continue;
            for (yy = y - 2; yy <= y + 2 && open; yy++)
                for (xx = x - 2; xx <= x + 2 && open; xx++)
                    if (!cave_have_flag_bold(yy, xx, FF_MOVE) || cave[yy][xx].m_idx) open = FALSE;
            if (!open) continue;
            best_d = d;
            by = y;
            bx = x;
        }
    }
    if (best_d < 999 && (by != py || bx != px))
        move_player_effect(by, bx, MPE_DONT_PICKUP | MPE_HANDLE_STUFF);
}

/* Group mode: the nearest living member of the tagged group, or 0 */
static int _wiz_kite_focus(u16b tag, int *alive, int *adjacent, bool *flanked)
{
    int i, best = 0, best_d = 999;
    int ady[8], adx[8], adj = 0, a, b;

    *alive = 0;
    for (i = 1; i < m_max; i++)
    {
        monster_type *m_ptr = &m_list[i];
        if (!m_ptr->r_idx || m_ptr->nickname != tag) continue;
        (*alive)++;
        if (m_ptr->cdis <= 1 && adj < 8)
        {
            ady[adj] = m_ptr->fy - py;
            adx[adj++] = m_ptr->fx - px;
        }
        if (m_ptr->cdis < best_d)
        {
            best_d = m_ptr->cdis;
            best = i;
        }
    }
    *adjacent = adj;
    /* Flanked: two attackers on opposite sides (more than 90 degrees apart) */
    *flanked = FALSE;
    for (a = 0; a < adj; a++)
        for (b = a + 1; b < adj; b++)
            if (ady[a] * ady[b] + adx[a] * adx[b] < 0) *flanked = TRUE;
    return best;
}

/* Group mode: place the race with its friends/escorts 5-8 squares away */
static int _wiz_kite_place_group(int r_idx, u16b tag)
{
    int tries, i;
    for (tries = 0; tries < 2000; tries++)
    {
        int y = py + randint0(17) - 8, x = px + randint0(17) - 8;
        int d = distance(py, px, y, x), m_idx;
        if (d < 5 || d > 8) continue;
        if (!in_bounds(y, x) || !cave_empty_bold(y, x)) continue;
        if (!place_monster_aux(0, y, x, r_idx, PM_NO_KAGE | PM_NO_PET | PM_ALLOW_GROUP)) continue;
        m_idx = cave[y][x].m_idx;
        if (!m_idx) continue;
        for (i = 1; i < m_max; i++)
        {
            monster_type *m_ptr = &m_list[i];
            if (!m_ptr->r_idx || i == p_ptr->riding) continue;
            (void)set_monster_csleep(i, 0);
            m_ptr->mflag &= ~MFLAG_NICE;
            m_ptr->nickname = tag;
        }
        return m_idx;
    }
    return 0;
}

static int _wiz_kite_place(int r_idx)
{
    int d, start = randint0(8);
    for (d = 0; d < 8; d++)
    {
        int dir = (start + d) % 8;
        int y = py + ddy_ddd[dir], x = px + ddx_ddd[dir];
        int m_idx;
        if (!in_bounds(y, x) || !cave_empty_bold(y, x)) continue;
        if (!place_monster_aux(0, y, x, r_idx, PM_NO_KAGE | PM_NO_PET)) continue;
        m_idx = cave[y][x].m_idx;
        if (!m_idx) continue;
        (void)set_monster_csleep(m_idx, 0);
        m_list[m_idx].mflag &= ~MFLAG_NICE;
        return m_idx;
    }
    return 0;
}

static void _wiz_ai_kite(void)
{
    int     r_idx, trials, turns, i, t, k;
    int     player_turns = 0, player_adjacent = 0, fails = 0, player_energy;
    int     test_speed = p_ptr->pspeed;
    int     found = 0, kills = 0, group_size = 0;
    s32b    adjacent_sum = 0, flanked_turns = 0;
    bool    group;
    s32b    find_turns = 0, kill_turns = 0;
    s32b    exp0 = p_ptr->exp, max_exp0 = p_ptr->max_exp;
    s32b    s_exp0 = p_ptr->exp, s_max_exp0 = p_ptr->max_exp;
    bool    chase, hide, fight, attack, bolt;
    char    buf[81];
    int     profile = 0, test_lev = 0, lev0 = p_ptr->lev, max_plv0 = p_ptr->max_plv;
    int     shots = 0;
    s32b    dmg_taken = 0, mhp_sum = 0;
    cptr    profile_name = "as is";
    int     start_y = py, start_x = px;
    int     orig_y = py, orig_x = px;
    s32b    old_game_turn = game_turn;
    byte    old_max;
    u32b    seed = 0, fingerprint = 0;
    bool    old_rand_quick = FALSE;
    u32b    old_rand_value = 0;
    u16b    old_rand_place = 0;
    u32b    old_rand_state[RAND_DEG];
    mon_ai_stats_t s;
    doc_ptr doc;
    u16b    kite_tag = quark_add("<kite>");

    if (p_ptr->inside_arena || p_ptr->inside_battle || p_ptr->wild_mode || p_ptr->riding)
    {
        msg_print("Not here.");
        return;
    }
    r_idx = _wiz_prompt_race("Monster? ");
    if (!r_idx) return;
    strcpy(buf, "50");
    if (!msg_input("Number of trials? ", buf, 10)) return;
    trials = atoi(buf);
    if (trials < 1) return;
    if (trials > 5000) trials = 5000;
    strcpy(buf, "1000");
    if (!msg_input("Game turns per trial? ", buf, 10)) return;
    turns = atoi(buf);
    if (turns < 10) turns = 10;
    if (turns > 20000) turns = 20000;
    strcpy(buf, "c");
    if (!msg_input("Player (c)hases, (f)ights, (a)ttacks (chase + melee), (b)olts, (s)tands still, or (t)eleports away and hides? ", buf, 2)) return;
    hide = (buf[0] == 't' || buf[0] == 'T');
    attack = (buf[0] == 'a' || buf[0] == 'A');
    bolt = (buf[0] == 'b' || buf[0] == 'B');
    fight = (buf[0] == 'f' || buf[0] == 'F') || attack || bolt;
    chase = !hide && (buf[0] != 's' && buf[0] != 'S') && !(buf[0] == 'f' || buf[0] == 'F');
    strcpy(buf, "n");
    if (!msg_input("Place its whole group, 5-8 squares away? (y/n) ", buf, 2)) return;
    group = (buf[0] == 'y' || buf[0] == 'Y');
    strcpy(buf, "0");
    if (!msg_input("Random seed (0 = don't fix)? ", buf, 12)) return;
    seed = strtoul(buf, NULL, 10);
    strcpy(buf, "1");
    if (!msg_input("Profile: (1) as is, (m)elee L25, (q)uick melee L25 +10 speed, (c)aster L25? ", buf, 2)) return;
    profile = buf[0];
    if (!get_check("This deletes every monster on the level, and the monster's attacks affect you for real (you cannot die). Continue? ")) return;

    if (seed)
    {
        old_rand_quick = Rand_quick;
        old_rand_value = Rand_value;
        old_rand_place = Rand_place;
        C_COPY(old_rand_state, Rand_state, RAND_DEG, u32b);
        Rand_quick = FALSE;
        Rand_state_init(seed);
    }

    /* Test profiles: a level 25 character with stand-in bonuses for mid-game
     * gear (applied in calc_bonuses; all undone at the end). Setting the
     * level directly skips level-up rewards and prompts. */
    switch (profile)
    {
    case 'm': case 'M':
        test_lev = 25; profile_name = "melee L25";
        wiz_bonus_to_h = 20; wiz_bonus_to_d = 12; wiz_bonus_dd = 2; wiz_bonus_blows = 150; wiz_bonus_ac = 60;
        break;
    case 'q': case 'Q':
        test_lev = 25; profile_name = "quick melee L25";
        wiz_bonus_to_h = 20; wiz_bonus_to_d = 12; wiz_bonus_dd = 2; wiz_bonus_blows = 150; wiz_bonus_ac = 60;
        wiz_bonus_speed = 10;
        break;
    case 'c': case 'C':
        test_lev = 25; profile_name = "caster L25";
        wiz_bonus_ac = 30;
        break;
    }
    if (test_lev)
    {
        p_ptr->max_plv = MAX(p_ptr->max_plv, PY_MAX_LEVEL);  /* no level-up rewards */
        exp0 = exp_requirement(test_lev - 1);
        max_exp0 = exp0;
    }

    old_max = r_info[r_idx].max_num;
    statistics_hack = TRUE;
    wiz_immortal = TRUE;
    if (group)
    {
        do_cmd_wiz_zap_all();
        _wiz_kite_open_ground();
        start_y = py;
        start_x = px;
    }
    WIPE(&mon_ai_stats, mon_ai_stats_t);

    for (i = 0; i < trials; i++)
    {
        int m_idx;

        do_cmd_wiz_zap_all();
        if ((py != start_y || px != start_x) && cave_empty_bold(start_y, start_x))
            move_player_effect(start_y, start_x, MPE_DONT_PICKUP | MPE_HANDLE_STUFF);
        /* Start every trial fresh: no slow, blindness, drained stats, ...
         * and recompute speed etc. even if nothing needed curing. Kills in
         * fight mode must not level the player up between trials. */
        p_ptr->exp = exp0;
        p_ptr->max_exp = max_exp0;
        check_experience();
        if (test_lev) p_ptr->lev = test_lev;
        do_cmd_wiz_cure_all();
        p_ptr->update |= PU_BONUS | PU_HP | PU_MANA;
        handle_stuff();
        p_ptr->chp = p_ptr->mhp;
        mhp_sum += p_ptr->mhp;
        if (i == 0) test_speed = p_ptr->pspeed;
        /* Uniques killed in an earlier trial come back for the next one
         * (max_num is restored when the test ends) */
        if (r_info[r_idx].flags1 & RF1_UNIQUE) r_info[r_idx].max_num = 1;
        m_idx = group ? _wiz_kite_place_group(r_idx, kite_tag) : _wiz_kite_place(r_idx);
        if (!m_idx)
        {
            fails++;
            continue;
        }
        mon_ai_stats.m_idx = m_idx;
        mon_ai_stats.pack_idx = group ? m_list[m_idx].pack_idx : 0;
        m_list[m_idx].nickname = kite_tag; /* detect the slot being reused after a kill */
        if (group)
        {
            int alive, adj;
            bool fl;
            (void)_wiz_kite_focus(kite_tag, &alive, &adj, &fl);
            group_size += alive;
        }
        player_energy = 0;

        /* Hide test: break contact with a medium-range teleport, then wait.
         * The monster has seen the player first, as it would in play. */
        if (hide)
        {
            mon_ai_perceive(&m_list[m_idx]);
            teleport_player(30, TELEPORT_PASSIVE);
            handle_stuff();
        }

        for (t = 0; t < turns; t++)
        {
            game_turn++;
            process_monsters();
            if (p_ptr->chp < p_ptr->mhp) dmg_taken += p_ptr->mhp - MAX(0, p_ptr->chp);
            p_ptr->chp = p_ptr->mhp;
            if (group)
            {
                /* Follow the nearest living member; the trial ends when all are dead */
                int alive, adj;
                bool fl;
                int focus = _wiz_kite_focus(kite_tag, &alive, &adj, &fl);
                if (focus) m_idx = focus;
                else m_list[m_idx].nickname = 0;  /* force the end-of-group check below */
            }
            if (!m_list[m_idx].r_idx || m_list[m_idx].nickname != kite_tag)
            {
                if (fight)
                {
                    kills++;
                    kill_turns += t;
                }
                break;
            }
            if (p_ptr->leaving || p_ptr->is_dead) break;
            if (hide && m_list[m_idx].cdis <= 1)
            {
                found++;
                find_turns += t;
                break;
            }

            /* The scripted player acts at its real speed */
            player_energy -= SPEED_TO_ENERGY(p_ptr->pspeed);
            if (player_energy <= 0)
            {
                player_energy += 100;
                player_turns++;
                if (group)
                {
                    int alive, adj;
                    bool fl;
                    (void)_wiz_kite_focus(kite_tag, &alive, &adj, &fl);
                    adjacent_sum += adj;
                    if (fl) flanked_turns++;
                }
                if (bolt && m_list[m_idx].ml && projectable(py, px, m_list[m_idx].fy, m_list[m_idx].fx))
                {
                    /* A generic attack spell: 7d8 at level 25, like a frost bolt */
                    if (m_list[m_idx].cdis <= 1) player_adjacent++;
                    shots++;
                    mon_ai_player_noise(MAI_NOISE_SPELL);
                    project(0, 0, m_list[m_idx].fy, m_list[m_idx].fx,
                        damroll(3 + (p_ptr->lev - 1) / 5, 8), GF_MISSILE,
                        PROJECT_STOP | PROJECT_KILL | PROJECT_REFLECTABLE | PROJECT_GRID);
                }
                else if (m_list[m_idx].cdis <= 1)
                {
                    player_adjacent++;  /* a melee chance for the player */
                    if (fight)
                        py_attack(m_list[m_idx].fy, m_list[m_idx].fx, 0);
                }
                else if (chase)
                    _wiz_player_chase_step(m_idx);
            }
        }
        mon_ai_stats.m_idx = 0;
        mon_ai_stats.pack_idx = 0;
        if (p_ptr->leaving || p_ptr->is_dead) break;
    }

    s = mon_ai_stats;
    WIPE(&mon_ai_stats, mon_ai_stats_t);
    do_cmd_wiz_zap_all();
    if ((py != orig_y || px != orig_x) && cave_empty_bold(orig_y, orig_x))
        move_player_effect(orig_y, orig_x, MPE_DONT_PICKUP | MPE_HANDLE_STUFF);
    wiz_bonus_to_h = wiz_bonus_to_d = wiz_bonus_dd = wiz_bonus_blows = wiz_bonus_ac = wiz_bonus_speed = 0;
    if (test_lev)
    {
        exp0 = s_exp0;
        max_exp0 = s_max_exp0;
        p_ptr->lev = lev0;
        p_ptr->max_plv = max_plv0;
    }
    p_ptr->exp = exp0;
    p_ptr->max_exp = max_exp0;
    check_experience();
    do_cmd_wiz_cure_all();
    p_ptr->update |= PU_BONUS | PU_HP | PU_MANA;
    handle_stuff();
    if ((py != start_y || px != start_x) && cave_empty_bold(start_y, start_x))
        move_player_effect(start_y, start_x, MPE_DONT_PICKUP | MPE_HANDLE_STUFF);
    wiz_immortal = FALSE;
    statistics_hack = FALSE;
    game_turn = old_game_turn;
    r_info[r_idx].max_num = old_max;
    if (seed)
    {
        fingerprint = _wiz_rng_fingerprint();
        Rand_quick = old_rand_quick;
        Rand_value = old_rand_value;
        Rand_place = old_rand_place;
        C_COPY(Rand_state, old_rand_state, RAND_DEG, u32b);
    }
    p_ptr->chp = p_ptr->mhp;
    p_ptr->update |= PU_MONSTERS | PU_BONUS | PU_HP;
    p_ptr->redraw |= PR_MAP | PR_HP;
    p_ptr->window |= PW_MONSTER_LIST;

    doc = doc_alloc(80);
    doc_printf(doc, "<color:G>AI Kite Test:</color> <color:y>%s</color> vs you (%s, speed %+d, %s)\n\n",
        r_name + r_info[r_idx].name,
        hide ? "hiding" : (bolt ? "bolting" : (attack ? "attacking" : (fight ? "fighting" : (chase ? "chasing" : "standing still")))),
        test_speed - 110, profile_name);
    if (bolt) doc_printf(doc, "You fired %d bolts\n", shots);
    if (player_turns && trials - fails > 0)
    {
        int avg_mhp = mhp_sum / (trials - fails);
        doc_printf(doc, "Damage taken: %d per 100 of your turns (%d%% of your %d max HP)\n",
            (int)(dmg_taken * 100 / player_turns), avg_mhp ? (int)(dmg_taken * 100 / player_turns * 100 / avg_mhp) : 0, avg_mhp);
    }
    doc_printf(doc, "%d trials x %d game turns", trials - fails, turns);
    if (fails) doc_printf(doc, " (%d could not be set up)", fails);
    doc_newline(doc);
    if (group && trials - fails > 0)
    {
        doc_printf(doc, "Group of <color:R>%d.%d</color> on average; per your turn <color:R>%d.%02d</color> of them next to you, flanked on <color:R>%d%%</color> of your turns\n",
            group_size / (trials - fails), (group_size * 10 / (trials - fails)) % 10,
            player_turns ? (int)(adjacent_sum / player_turns) : 0, player_turns ? (int)(adjacent_sum * 100 / player_turns % 100) : 0,
            player_turns ? (int)(flanked_turns * 100 / player_turns) : 0);
    }
    if (fight && trials - fails > 0)
    {
        doc_printf(doc, "You killed it in <color:R>%d%%</color> of trials", kills * 100 / (trials - fails));
        if (kills) doc_printf(doc, ", after <color:R>%d</color> game turns on average", kill_turns / kills);
        doc_newline(doc);
    }
    if (hide && trials - fails > 0)
    {
        doc_printf(doc, "Found you in <color:R>%d%%</color> of trials", found * 100 / (trials - fails));
        if (found) doc_printf(doc, ", after <color:R>%d</color> game turns on average", find_turns / found);
        doc_newline(doc);
    }
    if (s.turns)
    {
        int per = s.turns;
        doc_printf(doc, "Monster turns: %d, <color:R>%d%%</color> of them began next to you\n", s.turns, s.adjacent * 100 / per);
        doc_printf(doc, "Your turns: %d, <color:R>%d%%</color> of them next to the monster (melee chances)\n",
            player_turns, player_turns ? player_adjacent * 100 / player_turns : 0);
        doc_insert(doc, "\n<color:G>Per 100 monster turns:</color>\n");
        doc_printf(doc, "  Spells cast at you     %3d.%d\n", s.spells * 100 / per, (s.spells * 1000 / per) % 10);
        doc_printf(doc, "  Blinked away           %3d.%d\n", s.blinks * 100 / per, (s.blinks * 1000 / per) % 10);
        doc_printf(doc, "  Blinked you away       %3d.%d\n", s.blink_other * 100 / per, (s.blink_other * 1000 / per) % 10);
        doc_printf(doc, "  Teleported you away    %3d.%d\n", s.tele_other * 100 / per, (s.tele_other * 1000 / per) % 10);
        doc_printf(doc, "  Melee attacks on you   %3d.%d\n", s.melee * 100 / per, (s.melee * 1000 / per) % 10);
        doc_printf(doc, "  Charged big spells      %3d (released %d, interrupted %d)\n", s.charges, s.releases, s.interrupts);
        doc_printf(doc, "  Retreat turns           %3d.%d   Shouts for help %d   Orders barked %d\n", s.retreats * 100 / per, (s.retreats * 1000 / per) % 10, s.shouts, s.barks);
        doc_insert(doc, "\n<color:G>Perception at the start of its turns:</color>\n");
        for (k = 1; k < MAI_S_MAX; k++)
            doc_printf(doc, "  %-26s %3d.%d\n", mon_ai_state_name(k), s.states[k] * 100 / per, (s.states[k] * 1000 / per) % 10);
        doc_insert(doc, "\n<color:G>Turn decisions (spellcasters):</color>\n");
        for (k = 0; k < MAI_KIND_MAX; k++)
            doc_printf(doc, "  %-26s %3d.%d\n", mon_ai_kind_name(k), s.kinds[k] * 100 / per, (s.kinds[k] * 1000 / per) % 10);
    }
    if (seed)
        doc_printf(doc, "\nSeed %lu, RNG fingerprint <color:B>%08lX</color>\n", (unsigned long)seed, (unsigned long)fingerprint);
    doc_display(doc, "AI Kite Test", 0);
    doc_free(doc);
    do_cmd_redraw();
}

extern void do_cmd_debug(void);
void do_cmd_debug(void)
{
    int     x, y, n, repeat;
    char    cmd;

    if (REPEAT_PULL(&repeat))
        cmd = repeat;
    else
    {
        get_com("Debug Command: ", &cmd, FALSE);
        REPEAT_PUSH(cmd);
    }

    /* Analyze the command */
    switch (cmd)
    {
    /* Nothing */
    case ESCAPE:
    case ' ':
    case '\n':
    case '\r':
        break;

#ifdef ALLOW_SPOILERS

    /* Hack -- Generate Spoilers */
    case '"':
        do_cmd_spoilers();
        break;

#endif /* ALLOW_SPOILERS */

    /* Hack -- Help */
    case '?':
        do_cmd_help();
        break;

    /* Cure all maladies */
    case 'a':
        do_cmd_wiz_cure_all();
        break;

    /* Know alignment */
    case 'A':
        msg_format("Your alignment is %d.", p_ptr->align);
        break;

    /* Teleport to target */
    case 'b':
        do_cmd_wiz_bamf();
        break;

    case 'B':
        battle_monsters();
        break;

    /* Create any object */
    case 'c':
        wiz_create_item();
        break;

    /* Create a named artifact */
    case 'C':
    {
        char buf[81];
        buf[0] = 0;
        if (msg_input("Which artifact? ", buf, 80))
        {
            int idx = parse_lookup_artifact(buf, 0);
            if (!idx) idx = atoi(buf);
            wiz_create_named_art(idx);
        }
        break;
    }
    /* Detect everything */
    case 'd':
        detect_all(DETECT_RAD_ALL * 3);
        break;

    /* Dimension_door */
    case 'D':
        wiz_dimension_door();
        break;

    /* Edit character */
    case 'e':
        do_cmd_wiz_change();
        break;

    /* Blue-Mage spells */
    case 'E':
        if (p_ptr->pclass == CLASS_BLUE_MAGE) do_cmd_wiz_blue_mage();
        break;

    /* View item info */
    case 'f':
        identify_fully(NULL);
        break;

    /* Create desired feature */
    case 'F':
        do_cmd_wiz_create_feature();
        break;

    /* Good Objects */
    case 'g':
#if 1
    {
        object_type forge;
        int num = 10;

        while (num--)
        {
            object_wipe(&forge);
            if (!make_object(&forge, AM_GOOD, ORIGIN_CHEAT)) continue;
            drop_near(&forge, -1, py, px);
        }
    }
#else
        if (command_arg <= 0) command_arg = 10;
        acquirement(py, px, command_arg, FALSE, TRUE, ORIGIN_CHEAT);
#endif
        break;

    /* Hitpoint rerating */
    case 'h':
    {
        int i, r;
        int tot = 0, min = 76, max = 0;

        for (i = 0; i < 1000000; i++)
        {
            do_cmd_rerate_aux();
/*            r = life_rating();*/
            r = p_ptr->player_hp[PY_MAX_LEVEL - 1] / 10 - 223;
            tot += r;
            min = MIN(min, r);
            max = MAX(max, r);
        }
        /* The average is around 39.14 (aka 102.915%) */
//        msg_format("Life Ratings: %d%% (%d%%-%d%%)", tot/1000, min, max);
        msg_format("Life Ratings: %d.%02d (%d-%d%%)", tot/1000000, ((tot/10000) % 100), min, max);

    /*    do_cmd_rerate(TRUE); */

        break;
    }
#ifdef MONSTER_HORDES
    case 'H':
        do_cmd_summon_horde();
        break;
#endif /* MONSTER_HORDES */

    /* Identify */
    case 'i':
        (void)identify_fully(NULL);
        break;

    case 'I':
    {
        int i, ct = 0;
        char buf[MAX_NLEN];
        no_karrot_hack = TRUE;
        for (i = 0; i < max_o_idx; i++)
        {
            if (!o_list[i].k_idx) continue;
            ct++;
            obj_identify_fully(&o_list[i]);
            if (o_list[i].name1 || o_list[i].name2)
            {
                object_desc(buf, &o_list[i], 0);
                msg_print(buf);
            }
        }
        no_karrot_hack = FALSE;
        msg_format("Objects=%d", ct);
        break;
    }

    /* Go up or down in the dungeon */
    case 'j':
        do_cmd_wiz_jump();
        break;

    /* Self-Knowledge */
    case 'k':
        self_knowledge();
        break;

    /* Learn about objects */
    case 'l':
        do_cmd_wiz_learn();
        break;

    /* Magic Mapping */
    case 'm':
        map_area(DETECT_RAD_ALL * 3);
        (void)detect_monsters_invis(255);
        (void)detect_monsters_normal(255);
        break;

    /* Mutation */
    case 'M':
    {
    /*
        for (n = 0; n < 120; n++)
            mut_gain_random(NULL);*/
    /*  mut_gain_choice(mut_demigod_pred);*/
    /*  mut_gain_choice(mut_draconian_pred); */

      n = get_quantity("Which One? ", 500);
        if (n == 500)
        {
            int i;
            for (i = 0; i < 32; ++i)
                mut_gain(i);
        }
        else if (!mut_present(n)) mut_gain(n);
        else mut_lose(n);
        break;
    }

    /* Summon _friendly_ named monster */
    case 'N':
        do_cmd_wiz_named_friendly(command_arg);
        break;

    /* Summon Named Monster */
    case 'n':
    {
        char buf[81];
        buf[0] = 0;
        if (msg_input("Which monster? ", buf, 80))
        {
            int idx = parse_lookup_monster(buf, 0);
            if (!idx) idx = atoi(buf);
            do_cmd_wiz_named(idx);
        }
        break;
    }

    /* Object playing routines */
    case 'o':
        wiz_obj_smith();
        break;

    /* Phase Door */
    case 'p':
        teleport_player(10, 0L);
        break;

    /* Wizard Probe: AI inspector for the targeted monster */
    case 'P':
        if (target_who <= 0 || !m_list[target_who].r_idx)
        {
            if (!target_set(TARGET_KILL) || target_who <= 0) break;
        }
        {
            mon_ptr mon = &m_list[target_who];
            doc_ptr doc = doc_alloc(80);
            mon_ai_wizard(mon, doc);
            doc_display(doc, "Monster AI", 0);
            doc_free(doc);
            do_cmd_redraw();
        }
        break;

    /* Measure one monster's behaviour against the player */
    case 'K':
        _wiz_ai_kite();
        break;

    /* Run many monster-vs-monster fights and report win rates */
    case 'R':
        _wiz_ai_duel();
        break;

    /* AI summary of every visible monster */
    case 'Y':
    {
        doc_ptr doc = doc_alloc(100);
        mon_ai_wizard_summary(doc);
        doc_display(doc, "Monster AI Overview", 0);
        doc_free(doc);
        do_cmd_redraw();
        break;
    }
    case 'q':
    {
        quests_wizard();
        break;
    }
    /* Summon Random Monster(s) */
    case 's':
        if (command_arg <= 0) command_arg = 1;
        do_cmd_wiz_summon(command_arg);
        break;

    case 'S':
#ifdef ALLOW_SPOILERS
        generate_spoilers();
#endif
        break;

    /* Teleport */
    case 't':
        dimension_door(255);
        /*teleport_player(100, 0L);*/
        break;

    /* Make every dungeon square "known" to test streamers -KMW- */
    case 'u':
        for (y = 0; y < cur_hgt; y++)
        {
            for (x = 0; x < cur_wid; x++)
            {
                cave[y][x].info |= (CAVE_GLOW | CAVE_MARK | CAVE_AWARE);
            }
        }
        no_karrot_hack = TRUE;
        wiz_lite(FALSE);
        if (0) detect_treasure(255);
        if (1)
        {
            int i, ct = 0;
            /*char buf[MAX_NLEN];*/
            for (i = 0; i < max_o_idx; i++)
            {
                if (!o_list[i].k_idx) continue;
                if (o_list[i].tval == TV_GOLD) continue;
                ct += o_list[i].number;
                identify_item(&o_list[i]);
                obj_identify_fully(&o_list[i]);
                #if 0
                if (o_list[i].name1 || o_list[i].name2)
                {
                    object_desc(buf, &o_list[i], 0);
                    msg_print(buf);
                }
                #endif
            }
            if (0) msg_format("Objects=%d", ct);
        }
        no_karrot_hack = FALSE;
        break;


    /* Very Good Objects */
    case 'v':
        if (command_arg <= 0) command_arg = 1;
        acquirement(py, px, command_arg, TRUE, TRUE, ORIGIN_CHEAT);
        break;

    /* Wizard Light the Level */
    case 'w':
        wiz_lite(player_is_ninja);
        break;

    /* Increase Experience */
    case 'x':
        gain_exp(command_arg ? command_arg : (p_ptr->exp + 1));
        break;

    case 'X':
    {
        char tmp_val[80];
        long tmp_long;

        sprintf(tmp_val, "%d", p_ptr->exp);
        /* Query */
        if (!get_string("Experience: ", tmp_val, 10)) break;

        /* Extract */
        tmp_long = atol(tmp_val);

        /* Verify */
        if (tmp_long < 0) tmp_long = 0L;
        if (tmp_long > 99999999L) tmp_long = 99999999L;
        if (p_ptr->prace != RACE_ANDROID)
        {
            /* Save */
            p_ptr->max_exp = tmp_long;
            p_ptr->exp = tmp_long;

            /* Update */
            check_experience();
            p_ptr->max_plv = p_ptr->lev;
            p_ptr->max_max_exp = tmp_long;
            do_cmd_redraw();
        }
        break;
    }

    /* Zap Monsters (Genocide) */
    case 'z':
        do_cmd_wiz_zap();
        break;

    /* Zap Monsters (Omnicide) */
    case 'Z':
        do_cmd_wiz_zap_all();
        break;

    case '-':
    {
        /* Generate Statistics on object/monster distributions. Create a new
           character, run this command, then create a character dump
           or browse the object knowledge command (~2). Wizard commands "A and "O
           are also useful.*/
        int lev;
        int max_depth = get_quantity("Max Depth? ", 100);

        _wiz_stats_begin();
        _stats_reset_monster_levels();
        _stats_reset_object_levels();
        for (lev = MAX(1, dun_level); lev <= max_depth; lev += 1)
        {
            int reps = 1;

            if (lev % 10 == 0) reps += 1;
            if (lev % 20 == 0) reps += 1;
            if (lev % 30 == 0) reps += 2;

            _wiz_stats_gather(DUNGEON_ANGBAND, lev, reps);
        }
#if 1
        {
            _tally_t mon_total_tally = {0};
            _tally_t obj_total_tally = {0};
            int      obj_total = 0;
            int      obj_running = 0;
            int      last_lev = 0;

            for (lev = 0; lev < MAX_DEPTH; lev++)
                obj_total += _object_histogram[lev];

            doc_newline(_wiz_doc);
            doc_insert(_wiz_doc, "<color:G>Depth   Monster Level    Object Level   Object Counts</color>\n");
            for (lev = 0; lev < MAX_DEPTH; lev++)
            {
                _tally_t mon_tally = _monster_levels[lev];
                _tally_t obj_tally = _object_levels[lev];
                int      j, obj_ct = 0;

                if (!mon_tally.count || !obj_tally.count) continue;

                mon_total_tally.total += mon_tally.total;
                mon_total_tally.count += mon_tally.count;

                obj_total_tally.total += obj_tally.total;
                obj_total_tally.count += obj_tally.count;

                for (j = last_lev + 1; j <= lev; j++)
                {
                    obj_ct += _object_histogram[j];
                }
                last_lev = lev;
                obj_running += obj_ct;

                doc_printf(_wiz_doc, "%5d   %3d.%02d (%4d)    %3d.%02d (%4d)        %3d.%02d%%  %3d.%02d%%\n",
                    lev,
                    mon_tally.total / mon_tally.count,
                    (mon_tally.total*100 / mon_tally.count) % 100,
                    mon_tally.count,
                    obj_tally.total / obj_tally.count,
                    (obj_tally.total*100 / obj_tally.count) % 100,
                    obj_tally.count,
                    obj_ct*100 / obj_total,
                    (obj_ct*10000 / obj_total) % 100,
                    obj_running*100 / obj_total,
                    (obj_running*10000 / obj_total) % 100
                );
            }

            if (mon_total_tally.count && obj_total_tally.count)
            {
                doc_printf(_wiz_doc, "<color:R>        %3d.%02d (%5d)   %3d.%02d (%5d)</color>\n",
                    mon_total_tally.total / mon_total_tally.count,
                    (mon_total_tally.total*100 / mon_total_tally.count) % 100,
                    mon_total_tally.count,
                    obj_total_tally.total / obj_total_tally.count,
                    (obj_total_tally.total*100 / obj_total_tally.count) % 100,
                    obj_total_tally.count
                );
            }
            doc_newline(_wiz_doc);
        }
#endif
        _wiz_stats_end();
        _wiz_stats_display();
        _wiz_stats_free();

        break;
    }
    case '=':
    {
        /* In this version, we gather statistics on the current level of the
           current dungeon. You still want to start with a fresh character. */
        int reps = get_quantity("How many reps? ", 100);

        _wiz_stats_begin();
        _wiz_stats_gather(dungeon_type, dun_level, reps);
        _wiz_stats_end();
        _wiz_stats_display();
        _wiz_stats_free();

        break;
    }
    case '_': {
        int i;
        for (i = 0; i < 1000 * 1000; i++)
        {
            int roll = damroll(11,6);
            if (roll < 66*19/20) continue;
            msg_format("%d ", roll);
        }
        break; }
    default:
        msg_print("That is not a valid debug command.");
        break;
    }
}

#else

#ifdef MACINTOSH
static int i = 0;
#endif

#endif

