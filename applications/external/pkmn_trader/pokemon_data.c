#include <pokemon_icons.h>

#include "pokemon_data.h"
#include "pokemon_app.h"
#include "pokemon_char_encode.h"

#define RECALC_NONE 0x00
#define RECALC_EXP 0x01
#define RECALC_EVIVS 0x02
#define RECALC_STATS 0x04
#define RECALC_NICKNAME 0x08
#define RECALC_MOVES 0x10
#define RECALC_TYPES 0x20
#define RECALC_ALL 0xFF

struct __attribute__((__packed__)) named_list {
    const char* name;
    const uint8_t index;
    const uint8_t gen;
};

struct __attribute__((__packed__)) pokemon_data_table {
    const char* name;
    const Icon* icon;
    const uint8_t index;
    const uint8_t base_hp;
    const uint8_t base_atk;
    const uint8_t base_def;
    const uint8_t base_spd;
    const uint8_t base_spc;
    const uint8_t type[2];
    const uint8_t move[4];
    const uint8_t growth;
};

const NamedList move_list[];
const NamedList type_list[];
const NamedList stat_list[];
const PokemonTable pokemon_table[];

/* Allocates a chunk of memory for the trade data block and sets up some
 * default values.
 */
PokemonData* pokemon_data_alloc(uint8_t gen) {
    PokemonData* pdata;

    /* XXX: This will change depending on generation */
    pdata = malloc(sizeof(PokemonData));
    pdata->trade_block = malloc(sizeof(TradeBlockGenI));

    pdata->gen = gen;

    /* Clear struct to be all TERM_ bytes as the various name strings need this */
    memset(pdata->trade_block, TERM_, sizeof(TradeBlockGenI));

    /* The party_members element needs to be 0xff for unused */
    memset(
        ((TradeBlockGenI*)pdata->trade_block)->party_members,
        0xFF,
        sizeof(((TradeBlockGenI*)pdata->trade_block)->party_members));

    pdata->party = ((TradeBlockGenI*)pdata->trade_block)->party;

    /* Zero the main party data, TERM_ in there can cause weirdness */
    memset(pdata->party, 0x00, sizeof((*pdata->party)));

    /* Set our Name, the pokemon's default OT name and ID */
    ((TradeBlockGenI*)pdata->trade_block)->party_cnt = 1;

    /* Set up lists */
    pdata->move_list = move_list;
    pdata->type_list = type_list;
    pdata->stat_list = stat_list;
    pdata->pokemon_table = pokemon_table;

    /* Trainer/OT name, not to exceed 7 characters! */
    pokemon_name_set(pdata, STAT_TRAINER_NAME, "Flipper");
    pokemon_name_set(pdata, STAT_OT_NAME, "Flipper");

    /* OT trainer ID# */
    pokemon_stat_set(pdata, STAT_OT_ID, NONE, 42069);

    /* Notes:
     * Move pp isn't explicitly set up, should be fine
     * Catch/held isn't explicitly set up, should be okay for only Gen I support now
     * Status condition isn't explicity let up, would you ever want to?
     */

    /* Set up initial pokemon and level */
    /* This causes all other stats to be recalculated */
    pokemon_stat_set(pdata, STAT_NUM, NONE, 0); // First Pokemon
    pokemon_stat_set(pdata, STAT_LEVEL, NONE, 2); // Minimum level of 2
    pokemon_stat_set(pdata, STAT_CONDITION, NONE, 0); // No status conditions

    return pdata;
}

void pokemon_data_free(PokemonData* pdata) {
    free(pdata->trade_block);
    free(pdata);
}

/*******************************************
 * function declarations
 ******************************************/
static void pokemon_stat_ev_calc(PokemonData* pdata, EvIv val);
static void pokemon_stat_iv_calc(PokemonData* pdata, EvIv val);

/* XXX: EV/IV don't depend on anything other than what they are set to
 * by the ev/iv selection. Therefore, there is no reason to calculate
 * them here.
 * exp and stats are set from level.
 * stats are set from ev/iv.
 * ev requires level
 *
 * atk/def/spd/spc/hp require level, exp
 *
 * level: depends on: none
 * exp: depends on: level, index
 * iv: depends on: none
 * ev: depends on: level (sometimes)
 * atk/def/spd/spc/hp: depends on: level, ivs, evs, index
 * move: depends on: index
 * type: depends on: index
 * nickname: depends on: index
 */
void pokemon_recalculate(PokemonData* pdata, uint8_t recalc) {
    furi_assert(pdata);
    int i;

    if(recalc == RECALC_NONE) return;

    /* Ordered in order of priority for calculating other stats */
    if(recalc & RECALC_NICKNAME) pokemon_default_nickname_set(NULL, pdata, 0);

    if(recalc & RECALC_MOVES) {
        for(i = MOVE_0; i <= MOVE_3; i++) {
            pokemon_stat_set(
                pdata,
                STAT_MOVE,
                i,
                table_stat_base_get(pdata->pokemon_table, pdata, STAT_BASE_MOVE, i));
        }
    }

    if(recalc & RECALC_TYPES) {
        for(i = TYPE_0; i <= TYPE_1; i++) {
            pokemon_stat_set(
                pdata,
                STAT_TYPE,
                i,
                table_stat_base_get(pdata->pokemon_table, pdata, STAT_BASE_TYPE, i));
        }
    }

    if(recalc & RECALC_EXP) pokemon_exp_calc(pdata);

    if(recalc & RECALC_EVIVS) {
        pokemon_stat_ev_calc(pdata, pdata->stat_sel);
        pokemon_stat_iv_calc(pdata, pdata->stat_sel);
    }

    if(recalc & RECALC_STATS) {
        for(i = STAT; i < STAT_END; i++) {
            pokemon_stat_calc(pdata, i);
        }
    }
}

uint8_t namelist_gen_get_pos(const NamedList* list, uint8_t pos) {
    return list[pos].gen;
}

int namelist_cnt(const NamedList* list) {
    int i;

    for(i = 0;; i++) {
        if(list[i].name == NULL) return i;
    }
}

int namelist_pos_get(const NamedList* list, uint8_t index) {
    int i;

    for(i = 0;; i++) {
        if(list[i].name == NULL) break;
        if(index == list[i].index) return i;
    }

    /* This will return the first entry in case index is not matched.
     * Could be surprising at runtime.
     */
    return 0;
}

int namelist_index_get(const NamedList* list, uint8_t pos) {
    return list[pos].index;
}

const char* namelist_name_get_index(const NamedList* list, uint8_t index) {
    return list[namelist_pos_get(list, index)].name;
}

const char* namelist_name_get_pos(const NamedList* list, uint8_t pos) {
    return list[pos].name;
}

int table_pokemon_pos_get(const PokemonTable* table, uint8_t index) {
    int i;

    for(i = 0;; i++) {
        if(table[i].index == index) return i;
        if(table[i].name == NULL) break;
    }

    /* This will return the first entry in case index is not matched.
     * Could be surprising at runtime.
     */
    return 0;
}

const char* table_stat_name_get(const PokemonTable* table, int num) {
    return table[num].name;
}

/* This needs to convert to encoded characters */
void pokemon_name_set(PokemonData* pdata, DataStat stat, char* name) {
    furi_assert(pdata);
    size_t len;
    uint8_t* ptr = NULL;

    switch(stat) {
    case STAT_NICKNAME:
        ptr = ((TradeBlockGenI*)pdata->trade_block)->nickname[0].str;
        len = 10;
        break;
    case STAT_OT_NAME:
        ptr = ((TradeBlockGenI*)pdata->trade_block)->ot_name[0].str;
        len = 7;
        break;
    case STAT_TRAINER_NAME:
        ptr = ((TradeBlockGenI*)pdata->trade_block)->trainer_name.str;
        len = 7;
        break;
    default:
        furi_crash("name");
        break;
    }

    /* Clear the buffer with TERM character */
    memset(ptr, TERM_, LEN_NAME_BUF);

    /* Set the encoded name in the buffer */
    pokemon_str_to_encoded_array(ptr, name, len);
    FURI_LOG_D(TAG, "[data] %d name set to %s", stat, name);
}

void pokemon_name_get(PokemonData* pdata, DataStat stat, char* dest, size_t len) {
    furi_assert(pdata);
    uint8_t* ptr = NULL;

    switch(stat) {
    case STAT_NICKNAME:
        ptr = ((TradeBlockGenI*)pdata->trade_block)->nickname[0].str;
        break;
    case STAT_OT_NAME:
        ptr = ((TradeBlockGenI*)pdata->trade_block)->ot_name[0].str;
        break;
    default:
        furi_crash("name_get invalid");
        break;
    }

    pokemon_encoded_array_to_str(dest, ptr, len);
}

/* If dest is not NULL, a copy of the default name is written to it as well */
void pokemon_default_nickname_set(char* dest, PokemonData* pdata, size_t n) {
    furi_assert(pdata);
    unsigned int i;
    char buf[LEN_NAME_BUF];

    /* First, get the default name */
    strncpy(
        buf,
        table_stat_name_get(pdata->pokemon_table, pokemon_stat_get(pdata, STAT_NUM, NONE)),
        sizeof(buf));

    /* Next, walk through and toupper() each character */
    for(i = 0; i < sizeof(buf); i++) {
        buf[i] = toupper(buf[i]);
    }

    pokemon_name_set(pdata, STAT_NICKNAME, buf);
    FURI_LOG_D(TAG, "[data] Set default nickname");

    if(dest != NULL) {
        strncpy(dest, buf, n);
    }
}

/* XXX: This could also just pass the pokemon number? */
/* XXX: does no bounds checking of stat */
/* XXX: does no bounds checking of num */
uint8_t table_stat_base_get(
    const PokemonTable* table,
    PokemonData* pdata,
    DataStat stat,
    DataStatSub num) {
    furi_assert(pdata);
    int pkmnnum = pokemon_stat_get(pdata, STAT_NUM, NONE);

    switch(stat) {
    case STAT_BASE_ATK:
        return table[pkmnnum].base_hp;
    case STAT_BASE_DEF:
        return table[pkmnnum].base_def;
    case STAT_BASE_SPD:
        return table[pkmnnum].base_spd;
    case STAT_BASE_SPC:
        return table[pkmnnum].base_spc;
    case STAT_BASE_HP:
        return table[pkmnnum].base_hp;
    case STAT_BASE_TYPE:
        return table[pkmnnum].type[num];
    case STAT_BASE_MOVE:
        return table[pkmnnum].move[num];
    case STAT_BASE_GROWTH:
        return table[pkmnnum].growth;
    default:
        furi_crash("BASE_GET: invalid stat");
        break;
    }

    return 0;
}

const Icon* table_icon_get(const PokemonTable* table, int num) {
    return table[num].icon;
}

uint16_t pokemon_stat_get(PokemonData* pdata, DataStat stat, DataStatSub which) {
    furi_assert(pdata);
    void* party = pdata->party;
    int gen = pdata->gen;
    uint16_t val = 0;

    switch(stat) {
    case STAT_ATK:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->atk;
        break;
    case STAT_DEF:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->def;
        break;
    case STAT_SPD:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->spd;
        break;
    case STAT_SPC:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->spc;
        break;
    case STAT_HP:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->hp;
        break;
    case STAT_ATK_EV:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->atk_ev;
        break;
    case STAT_DEF_EV:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->def_ev;
        break;
    case STAT_SPD_EV:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->spd_ev;
        break;
    case STAT_SPC_EV:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->spc_ev;
        break;
    case STAT_HP_EV:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->hp_ev;
        break;
    case STAT_ATK_IV:
        if(gen == GEN_I) return (((PokemonPartyGenI*)party)->iv >> 12) & 0x0F;
        break;
    case STAT_DEF_IV:
        if(gen == GEN_I) return (((PokemonPartyGenI*)party)->iv >> 8) & 0x0F;
        break;
    case STAT_SPD_IV:
        if(gen == GEN_I) return (((PokemonPartyGenI*)party)->iv >> 4) & 0x0F;
        break;
    case STAT_SPC_IV:
        if(gen == GEN_I) return ((PokemonPartyGenI*)party)->iv & 0x0F;
        break;
    case STAT_HP_IV:
        if(gen == GEN_I) return (((PokemonPartyGenI*)party)->iv & 0xAA) >> 4;
        break;
    case STAT_LEVEL:
        if(gen == GEN_I) return ((PokemonPartyGenI*)party)->level;
        break;
    case STAT_INDEX:
        if(gen == GEN_I) return ((PokemonPartyGenI*)party)->index;
        break;
    case STAT_NUM:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->index;
        return table_pokemon_pos_get(pdata->pokemon_table, val);
    case STAT_MOVE:
        if(gen == GEN_I) return ((PokemonPartyGenI*)party)->move[which];
        break;
    case STAT_TYPE:
        if(gen == GEN_I) return ((PokemonPartyGenI*)party)->type[which];
        break;
    case STAT_OT_ID:
        if(gen == GEN_I) val = ((PokemonPartyGenI*)party)->ot_id;
        break;
    case STAT_SEL:
        if(gen == GEN_I) return pdata->stat_sel;
        break;
    case STAT_CONDITION:
        if(gen == GEN_I) return ((PokemonPartyGenI*)party)->status_condition = val;
        break;
    case STAT_GEN:
        return GEN_I;
    default:
        furi_crash("STAT_GET: invalid stat");
        break;
    }

    return __builtin_bswap16(val);
}

void pokemon_stat_set(PokemonData* pdata, DataStat stat, DataStatSub which, uint16_t val) {
    furi_assert(pdata);
    void* party = pdata->party;
    int gen = pdata->gen;
    uint8_t recalc = 0;
    uint16_t val_swap = __builtin_bswap16(val);

    switch(stat) {
    case STAT_ATK:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->atk = val_swap;
        break;
    case STAT_DEF:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->def = val_swap;
        break;
    case STAT_SPD:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->spd = val_swap;
        break;
    case STAT_SPC:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->spc = val_swap;
        break;
    case STAT_HP:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->hp = val_swap;
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->max_hp = val_swap;
        break;
    case STAT_ATK_EV:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->atk_ev = val_swap;
        break;
    case STAT_DEF_EV:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->def_ev = val_swap;
        break;
    case STAT_SPD_EV:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->spd_ev = val_swap;
        break;
    case STAT_SPC_EV:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->spc_ev = val_swap;
        break;
    case STAT_HP_EV:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->hp_ev = val_swap;
        break;
    case STAT_IV:
        /* This is assumed to always be:
	 * atk, def, spd, spc
	 * each taking up 4 bits of 16.
	 */
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->iv = val;
        break;
    case STAT_MOVE:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->move[which] = val;
        break;
    case STAT_TYPE:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->type[which] = val;
        break;
    case STAT_LEVEL:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->level = val;
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->level_again = val;
        recalc = (RECALC_STATS | RECALC_EXP | RECALC_EVIVS);
        break;
    case STAT_INDEX:
        if(gen == GEN_I) {
            ((PokemonPartyGenI*)party)->index = val;
            ((TradeBlockGenI*)pdata->trade_block)->party_members[0] = val;
        }
        recalc = RECALC_ALL; // Always recalculate everything if we selected a different pokemon
        break;
    case STAT_NUM:
        pokemon_stat_set(pdata, STAT_INDEX, NONE, pdata->pokemon_table[val].index);
        break;
    case STAT_OT_ID:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->ot_id = val_swap;
        break;
    case STAT_SEL:
        if(gen == GEN_I) pdata->stat_sel = val;
        recalc = (RECALC_EVIVS | RECALC_STATS);
        break;
    case STAT_EXP:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->exp[which] = val;
        break;
    case STAT_CONDITION:
        if(gen == GEN_I) ((PokemonPartyGenI*)party)->status_condition = val;
        break;
    case STAT_GEN:
        /* XXX: This needs to go away */
        break;
    default:
        furi_crash("STAT_SET: invalid stat");
        break;
    }
    FURI_LOG_D(TAG, "[data] stat %d:%d set to %d", stat, which, val);
    pokemon_recalculate(pdata, recalc);
}

uint16_t pokemon_stat_ev_get(PokemonData* pdata, DataStat stat) {
    furi_assert(pdata);
    DataStat next;

    switch(stat) {
    case STAT_ATK:
        next = STAT_ATK_EV;
        break;
    case STAT_DEF:
        next = STAT_DEF_EV;
        break;
    case STAT_SPD:
        next = STAT_SPD_EV;
        break;
    case STAT_SPC:
        next = STAT_SPC_EV;
        break;
    case STAT_HP:
        next = STAT_HP_EV;
        break;
    default:
        furi_crash("EV_GET: invalid stat");
        return 0;
    }
    return pokemon_stat_get(pdata, next, NONE);
}

static void pokemon_stat_ev_calc(PokemonData* pdata, EvIv val) {
    furi_assert(pdata);
    int level;
    uint16_t ev;
    DataStat i;

    level = pokemon_stat_get(pdata, STAT_LEVEL, NONE);

    /* Generate STATEXP */
    switch(val) {
    case RANDIV_LEVELEV:
    case MAXIV_LEVELEV:
        ev = (0xffff / 100) * level;
        break;
    case RANDIV_MAXEV:
    case MAXIV_MAXEV:
        ev = 0xffff;
        break;
    default:
        ev = 0;
        break;
    }

    for(i = STAT_EV; i < STAT_EV_END; i++) {
        pokemon_stat_set(pdata, i, NONE, ev);
    }
}

uint8_t pokemon_stat_iv_get(PokemonData* pdata, DataStat stat) {
    furi_assert(pdata);
    DataStat next;

    switch(stat) {
    case STAT_ATK:
        next = STAT_ATK_IV;
        break;
    case STAT_DEF:
        next = STAT_DEF_IV;
        break;
    case STAT_SPD:
        next = STAT_SPD_IV;
        break;
    case STAT_SPC:
        next = STAT_SPC_IV;
        break;
    case STAT_HP:
        next = STAT_HP_IV;
        break;
    default:
        furi_crash("IV_GET: invalid stat");
        return 0;
    }
    return pokemon_stat_get(pdata, next, NONE);
}

static void pokemon_stat_iv_calc(PokemonData* pdata, EvIv val) {
    furi_assert(pdata);
    uint16_t iv;

    /* Set up IVs */
    switch(val) {
    case RANDIV_ZEROEV:
    case RANDIV_LEVELEV:
    case RANDIV_MAXEV:
        iv = (uint16_t)rand();
        break;
    default:
        iv = 0xFFFF;
        break;
    }

    pokemon_stat_set(pdata, STAT_IV, NONE, iv);
}

/* XXX: Could offload these args and use the different *_get() functions in here instead */
static uint16_t stat_calc(uint8_t base, uint8_t iv, uint16_t ev, uint8_t level, DataStat stat) {
    uint16_t tmp;
    /* Gen I calculation */
    // https://bulbapedia.bulbagarden.net/wiki/Stat#Generations_I_and_II
    tmp = floor((((2 * (base + iv)) + floor(sqrt(ev) / 4)) * level) / 100);
    /* HP */
    if(stat == STAT_HP) tmp += (level + 10);
    /* All other stats */
    else
        tmp += 5;

    return tmp;
}

#define UINT32_TO_EXP(input, output_array)                     \
    do {                                                       \
        (output_array)[2] = (uint8_t)((input) & 0xFF);         \
        (output_array)[1] = (uint8_t)(((input) >> 8) & 0xFF);  \
        (output_array)[0] = (uint8_t)(((input) >> 16) & 0xFF); \
    } while(0)

void pokemon_exp_set(PokemonData* pdata, uint32_t exp) {
    furi_assert(pdata);
    uint8_t exp_tmp[3];
    int i;

    UINT32_TO_EXP(exp, exp_tmp);

    for(i = EXP_0; i <= EXP_2; i++) {
        pokemon_stat_set(pdata, STAT_EXP, i, exp_tmp[i]);
    }

    FURI_LOG_D(TAG, "[data] Set pkmn exp %d", (int)exp);
}

void pokemon_exp_calc(PokemonData* pdata) {
    furi_assert(pdata);
    int level;
    uint32_t exp;
    uint8_t growth = table_stat_base_get(pdata->pokemon_table, pdata, STAT_BASE_GROWTH, NONE);

    level = (int)pokemon_stat_get(pdata, STAT_LEVEL, NONE);
    /* Calculate exp */
    switch(growth) {
    case GROWTH_FAST:
        // https://bulbapedia.bulbagarden.net/wiki/Experience#Fast
        exp = (4 * level * level * level) / 5;
        break;
    case GROWTH_MEDIUM_FAST:
        // https://bulbapedia.bulbagarden.net/wiki/Experience#Medium_Fast
        exp = (level * level * level);
        break;
    case GROWTH_MEDIUM_SLOW:
        // https://bulbapedia.bulbagarden.net/wiki/Experience#Medium_Slow
        exp = (((level * level * level) * 6 / 5) - (15 * level * level) + (100 * level) - 140);
        break;
    case GROWTH_SLOW:
        // https://bulbapedia.bulbagarden.net/wiki/Experience#Slow
        exp = (5 * level * level * level) / 4;
        break;
    default:
        furi_crash("incorrect growth val");
        break;
    }

    pokemon_exp_set(pdata, exp);
}

/* Calculates stat from current level */
/* XXX: Would it make sense instead to have a single function get the right bases and stats? */
void pokemon_stat_calc(PokemonData* pdata, DataStat stat) {
    furi_assert(pdata);
    uint8_t stat_iv;
    uint16_t stat_ev;
    uint16_t stat_tmp;
    uint8_t stat_base;
    uint8_t level;

    level = pokemon_stat_get(pdata, STAT_LEVEL, NONE);
    stat_base = table_stat_base_get(pdata->pokemon_table, pdata, stat, NONE);
    stat_ev = pokemon_stat_ev_get(pdata, stat);
    stat_iv = pokemon_stat_iv_get(pdata, stat);
    stat_tmp = stat_calc(stat_base, stat_iv, stat_ev, level, stat);

    pokemon_stat_set(pdata, stat, NONE, stat_tmp);
}

void pokemon_stat_memcpy(PokemonData* dst, void* traded, uint8_t which) {
    /* Copy the traded-in Pokemon's main data to our struct */
    /* XXX: Can use pokemon_stat_set */
    /* XXX: TODO: While slower, want to implement this as a handful of functions to
    * get from the traded struct and set the main struct.
    */
    ((TradeBlockGenI*)dst->trade_block)->party_members[0] =
        ((TradeBlockGenI*)traded)->party_members[which];
    memcpy(
        &(((TradeBlockGenI*)dst->trade_block)->party[0]),
        &(((TradeBlockGenI*)traded)->party[which]),
        sizeof(PokemonPartyGenI));
    memcpy(
        &(((TradeBlockGenI*)dst->trade_block)->nickname[0]),
        &(((TradeBlockGenI*)traded)->nickname[which]),
        sizeof(struct name));
    memcpy(
        &(((TradeBlockGenI*)dst->trade_block)->ot_name[0]),
        &(((TradeBlockGenI*)traded)->ot_name[which]),
        sizeof(struct name));
}

const PokemonTable pokemon_table[] = {
    /* Values for base_*, moves, etc., pulled directly from a copy of Pokemon Blue */
    {"Bulbasaur",
     &I_bulbasaur,
     0x99,
     0x2D,
     0x31,
     0x31,
     0x2D,
     0x41,
     {0x16, 0x03},
     {0x21, 0x2D, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Ivysaur",
     &I_ivysaur,
     0x09,
     0x3C,
     0x3E,
     0x3F,
     0x3C,
     0x50,
     {0x16, 0x03},
     {0x21, 0x2D, 0x49, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Venusaur",
     &I_venusaur,
     0x9A,
     0x50,
     0x52,
     0x53,
     0x50,
     0x64,
     {0x16, 0x03},
     {0x21, 0x2D, 0x49, 0x16},
     GROWTH_MEDIUM_SLOW},
    {"Charmander",
     &I_charmander,
     0xB0,
     0x27,
     0x34,
     0x2B,
     0x41,
     0x32,
     {0x14, 0x14},
     {0x0A, 0x2D, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Charmeleon",
     &I_charmeleon,
     0xB2,
     0x3A,
     0x40,
     0x3A,
     0x50,
     0x41,
     {0x14, 0x14},
     {0x0A, 0x2D, 0x34, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Charizard",
     &I_charizard,
     0xB4,
     0x4E,
     0x54,
     0x4E,
     0x64,
     0x55,
     {0x14, 0x02},
     {0x0A, 0x2D, 0x34, 0x2B},
     GROWTH_MEDIUM_SLOW},
    {"Squirtle",
     &I_squirtle,
     0xB1,
     0x2C,
     0x30,
     0x41,
     0x2B,
     0x32,
     {0x15, 0x15},
     {0x21, 0x27, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Wartortle",
     &I_wartortle,
     0xB3,
     0x3B,
     0x3F,
     0x50,
     0x3A,
     0x41,
     {0x15, 0x15},
     {0x21, 0x27, 0x91, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Blastoise",
     &I_blastoise,
     0x1C,
     0x4F,
     0x53,
     0x64,
     0x4E,
     0x55,
     {0x15, 0x15},
     {0x21, 0x27, 0x91, 0x37},
     GROWTH_MEDIUM_SLOW},
    {"Caterpie",
     &I_caterpie,
     0x7B,
     0x2D,
     0x1E,
     0x23,
     0x2D,
     0x14,
     {0x07, 0x07},
     {0x21, 0x51, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Metapod",
     &I_metapod,
     0x7C,
     0x32,
     0x14,
     0x37,
     0x1E,
     0x19,
     {0x07, 0x07},
     {0x6A, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Butterfree",
     &I_butterfree,
     0x7D,
     0x3C,
     0x2D,
     0x32,
     0x46,
     0x50,
     {0x07, 0x02},
     {0x5D, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Weedle",
     &I_weedle,
     0x70,
     0x28,
     0x23,
     0x1E,
     0x32,
     0x14,
     {0x07, 0x03},
     {0x28, 0x51, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Kakuna",
     &I_kakuna,
     0x71,
     0x2D,
     0x19,
     0x32,
     0x23,
     0x19,
     {0x07, 0x03},
     {0x6A, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Beedrill",
     &I_beedrill,
     0x72,
     0x41,
     0x50,
     0x28,
     0x4B,
     0x2D,
     {0x07, 0x03},
     {0x1F, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Pidgey",
     &I_pidgey,
     0x24,
     0x28,
     0x2D,
     0x28,
     0x38,
     0x23,
     {0x00, 0x02},
     {0x10, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Pidgeotto",
     &I_pidgeotto,
     0x96,
     0x3F,
     0x3C,
     0x37,
     0x47,
     0x32,
     {0x00, 0x02},
     {0x10, 0x1C, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Pidgeot",
     &I_pidgeot,
     0x97,
     0x53,
     0x50,
     0x4B,
     0x5B,
     0x46,
     {0x00, 0x02},
     {0x10, 0x1C, 0x62, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Rattata",
     &I_rattata,
     0xA5,
     0x1E,
     0x38,
     0x23,
     0x48,
     0x19,
     {0x00, 0x00},
     {0x21, 0x27, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Raticate",
     &I_raticate,
     0xA6,
     0x37,
     0x51,
     0x3C,
     0x61,
     0x32,
     {0x00, 0x00},
     {0x21, 0x27, 0x62, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Spearow",
     &I_spearow,
     0x05,
     0x28,
     0x3C,
     0x1E,
     0x46,
     0x1F,
     {0x00, 0x02},
     {0x40, 0x2D, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Fearow",
     &I_fearow,
     0x23,
     0x41,
     0x5A,
     0x41,
     0x64,
     0x3D,
     {0x00, 0x02},
     {0x40, 0x2D, 0x2B, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Ekans",
     &I_ekans,
     0x6C,
     0x23,
     0x3C,
     0x2C,
     0x37,
     0x28,
     {0x03, 0x03},
     {0x23, 0x2B, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Arbok",
     &I_arbok,
     0x2D,
     0x3C,
     0x55,
     0x45,
     0x50,
     0x41,
     {0x03, 0x03},
     {0x23, 0x2B, 0x28, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Pikachu",
     &I_pikachu,
     0x54,
     0x23,
     0x37,
     0x1E,
     0x5A,
     0x32,
     {0x17, 0x17},
     {0x54, 0x2D, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Raichu",
     &I_raichu,
     0x55,
     0x3C,
     0x5A,
     0x37,
     0x64,
     0x5A,
     {0x17, 0x17},
     {0x54, 0x2D, 0x56, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Sandshrew",
     &I_sandshrew,
     0x60,
     0x32,
     0x4B,
     0x55,
     0x28,
     0x1E,
     {0x04, 0x04},
     {0x0A, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Sandslash",
     &I_sandslash,
     0x61,
     0x4B,
     0x64,
     0x6E,
     0x41,
     0x37,
     {0x04, 0x04},
     {0x0A, 0x1C, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Nidoran\200",
     &I_nidoranf,
     0x0F,
     0x37,
     0x2F,
     0x34,
     0x29,
     0x28,
     {0x03, 0x03},
     {0x2D, 0x21, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Nidorina",
     &I_nidorina,
     0xA8,
     0x46,
     0x3E,
     0x43,
     0x38,
     0x37,
     {0x03, 0x03},
     {0x2D, 0x21, 0x0A, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Nidoqueen",
     &I_nidoqueen,
     0x10,
     0x5A,
     0x52,
     0x57,
     0x4C,
     0x4B,
     {0x03, 0x04},
     {0x21, 0x0A, 0x27, 0x22},
     GROWTH_MEDIUM_SLOW},
    {"Nidoran\201",
     &I_nidoranm,
     0x03,
     0x2E,
     0x39,
     0x28,
     0x32,
     0x28,
     {0x03, 0x03},
     {0x2B, 0x21, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Nidorino",
     &I_nidorino,
     0xA7,
     0x3D,
     0x48,
     0x39,
     0x41,
     0x37,
     {0x03, 0x03},
     {0x2B, 0x21, 0x1E, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Nidoking",
     &I_nidoking,
     0x07,
     0x51,
     0x5C,
     0x4D,
     0x55,
     0x4B,
     {0x03, 0x04},
     {0x21, 0x1E, 0x28, 0x25},
     GROWTH_MEDIUM_SLOW},
    {"Clefairy",
     &I_clefairy,
     0x04,
     0x46,
     0x2D,
     0x30,
     0x23,
     0x3C,
     {0x00, 0x00},
     {0x01, 0x2D, 0x00, 0x00},
     GROWTH_FAST},
    {"Clefable",
     &I_clefable,
     0x8E,
     0x5F,
     0x46,
     0x49,
     0x3C,
     0x55,
     {0x00, 0x00},
     {0x2F, 0x03, 0x6B, 0x76},
     GROWTH_FAST},
    {"Vulpix",
     &I_vulpix,
     0x52,
     0x26,
     0x29,
     0x28,
     0x41,
     0x41,
     {0x14, 0x14},
     {0x34, 0x27, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Ninetales",
     &I_ninetales,
     0x53,
     0x49,
     0x4C,
     0x4B,
     0x64,
     0x64,
     {0x14, 0x14},
     {0x34, 0x27, 0x62, 0x2E},
     GROWTH_MEDIUM_FAST},
    {"Jigglypuff",
     &I_jigglypuff,
     0x64,
     0x73,
     0x2D,
     0x14,
     0x14,
     0x19,
     {0x00, 0x00},
     {0x2F, 0x00, 0x00, 0x00},
     GROWTH_FAST},
    {"Wigglytuff",
     &I_wigglytuff,
     0x65,
     0x8C,
     0x46,
     0x2D,
     0x2D,
     0x32,
     {0x00, 0x00},
     {0x2F, 0x32, 0x6F, 0x03},
     GROWTH_FAST},
    {"Zubat",
     &I_zubat,
     0x6B,
     0x28,
     0x2D,
     0x23,
     0x37,
     0x28,
     {0x03, 0x02},
     {0x8D, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Golbat",
     &I_golbat,
     0x82,
     0x4B,
     0x50,
     0x46,
     0x5A,
     0x4B,
     {0x03, 0x02},
     {0x8D, 0x67, 0x2C, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Oddish",
     &I_oddish,
     0xB9,
     0x2D,
     0x32,
     0x37,
     0x1E,
     0x4B,
     {0x16, 0x03},
     {0x47, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Gloom",
     &I_gloom,
     0xBA,
     0x3C,
     0x41,
     0x46,
     0x28,
     0x55,
     {0x16, 0x03},
     {0x47, 0x4D, 0x4E, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Vileplume",
     &I_vileplume,
     0xBB,
     0x4B,
     0x50,
     0x55,
     0x32,
     0x64,
     {0x16, 0x03},
     {0x4E, 0x4F, 0x33, 0x50},
     GROWTH_MEDIUM_SLOW},
    {"Paras",
     &I_paras,
     0x6D,
     0x23,
     0x46,
     0x37,
     0x19,
     0x37,
     {0x07, 0x16},
     {0x0A, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Parasect",
     &I_parasect,
     0x2E,
     0x3C,
     0x5F,
     0x50,
     0x1E,
     0x50,
     {0x07, 0x16},
     {0x0A, 0x4E, 0x8D, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Venonat",
     &I_venonat,
     0x41,
     0x3C,
     0x37,
     0x32,
     0x2D,
     0x28,
     {0x07, 0x03},
     {0x21, 0x32, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Venomoth",
     &I_venomoth,
     0x77,
     0x46,
     0x41,
     0x3C,
     0x5A,
     0x5A,
     {0x07, 0x03},
     {0x21, 0x32, 0x4D, 0x8D},
     GROWTH_MEDIUM_FAST},
    {"Diglett",
     &I_diglett,
     0x3B,
     0x0A,
     0x37,
     0x19,
     0x5F,
     0x2D,
     {0x04, 0x04},
     {0x0A, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Dugtrio",
     &I_dugtrio,
     0x76,
     0x23,
     0x50,
     0x32,
     0x78,
     0x46,
     {0x04, 0x04},
     {0x0A, 0x2D, 0x5B, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Meowth",
     &I_meowth,
     0x4D,
     0x28,
     0x2D,
     0x23,
     0x5A,
     0x28,
     {0x00, 0x00},
     {0x0A, 0x2D, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Persian",
     &I_persian,
     0x90,
     0x41,
     0x46,
     0x3C,
     0x73,
     0x41,
     {0x00, 0x00},
     {0x0A, 0x2D, 0x2C, 0x67},
     GROWTH_MEDIUM_FAST},
    {"Psyduck",
     &I_psyduck,
     0x2F,
     0x32,
     0x34,
     0x30,
     0x37,
     0x32,
     {0x15, 0x15},
     {0x0A, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Golduck",
     &I_golduck,
     0x80,
     0x50,
     0x52,
     0x4E,
     0x55,
     0x50,
     {0x15, 0x15},
     {0x0A, 0x27, 0x32, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Mankey",
     &I_mankey,
     0x39,
     0x28,
     0x50,
     0x23,
     0x46,
     0x23,
     {0x01, 0x01},
     {0x0A, 0x2B, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Primeape",
     &I_primeape,
     0x75,
     0x41,
     0x69,
     0x3C,
     0x5F,
     0x3C,
     {0x01, 0x01},
     {0x0A, 0x2B, 0x02, 0x9A},
     GROWTH_MEDIUM_FAST},
    {"Growlithe",
     &I_growlithe,
     0x21,
     0x37,
     0x46,
     0x2D,
     0x3C,
     0x32,
     {0x14, 0x14},
     {0x2C, 0x2E, 0x00, 0x00},
     GROWTH_SLOW},
    {"Arcanine",
     &I_arcanine,
     0x14,
     0x5A,
     0x6E,
     0x50,
     0x5F,
     0x50,
     {0x14, 0x14},
     {0x2E, 0x34, 0x2B, 0x24},
     GROWTH_SLOW},
    {"Poliwag",
     &I_poliwag,
     0x47,
     0x28,
     0x32,
     0x28,
     0x5A,
     0x28,
     {0x15, 0x15},
     {0x91, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Poliwhirl",
     &I_poliwhirl,
     0x6E,
     0x41,
     0x41,
     0x41,
     0x5A,
     0x32,
     {0x15, 0x15},
     {0x91, 0x5F, 0x37, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Poliwrath",
     &I_poliwrath,
     0x6F,
     0x5A,
     0x55,
     0x5F,
     0x46,
     0x46,
     {0x15, 0x01},
     {0x5F, 0x37, 0x03, 0x22},
     GROWTH_MEDIUM_SLOW},
    {"Abra",
     &I_abra,
     0x94,
     0x19,
     0x14,
     0x0F,
     0x5A,
     0x69,
     {0x18, 0x18},
     {0x64, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Kadabra",
     &I_kadabra,
     0x26,
     0x28,
     0x23,
     0x1E,
     0x69,
     0x78,
     {0x18, 0x18},
     {0x64, 0x5D, 0x32, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Alakazam",
     &I_alakazam,
     0x95,
     0x37,
     0x32,
     0x2D,
     0x78,
     0x87,
     {0x18, 0x18},
     {0x64, 0x5D, 0x32, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Machop",
     &I_machop,
     0x6A,
     0x46,
     0x50,
     0x32,
     0x23,
     0x23,
     {0x01, 0x01},
     {0x02, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Machoke",
     &I_machoke,
     0x29,
     0x50,
     0x64,
     0x46,
     0x2D,
     0x32,
     {0x01, 0x01},
     {0x02, 0x43, 0x2B, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Machamp",
     &I_machamp,
     0x7E,
     0x5A,
     0x82,
     0x50,
     0x37,
     0x41,
     {0x01, 0x01},
     {0x02, 0x43, 0x2B, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Bellsprout",
     &I_bellsprout,
     0xBC,
     0x32,
     0x4B,
     0x23,
     0x28,
     0x46,
     {0x16, 0x03},
     {0x16, 0x4A, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Weepinbell",
     &I_weepinbell,
     0xBD,
     0x41,
     0x5A,
     0x32,
     0x37,
     0x55,
     {0x16, 0x03},
     {0x16, 0x4A, 0x23, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Victreebel",
     &I_victreebel,
     0xBE,
     0x50,
     0x69,
     0x41,
     0x46,
     0x64,
     {0x16, 0x03},
     {0x4F, 0x4E, 0x33, 0x4B},
     GROWTH_MEDIUM_SLOW},
    {"Tentacool",
     &I_tentacool,
     0x18,
     0x28,
     0x28,
     0x23,
     0x46,
     0x64,
     {0x15, 0x03},
     {0x33, 0x00, 0x00, 0x00},
     GROWTH_SLOW},
    {"Tentacruel",
     &I_tentacruel,
     0x9B,
     0x50,
     0x46,
     0x41,
     0x64,
     0x78,
     {0x15, 0x03},
     {0x33, 0x30, 0x23, 0x00},
     GROWTH_SLOW},
    {"Geodude",
     &I_geodude,
     0xA9,
     0x28,
     0x50,
     0x64,
     0x14,
     0x1E,
     {0x05, 0x04},
     {0x21, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Graveler",
     &I_graveler,
     0x27,
     0x37,
     0x5F,
     0x73,
     0x23,
     0x2D,
     {0x05, 0x04},
     {0x21, 0x6F, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Golem",
     &I_golem,
     0x31,
     0x50,
     0x6E,
     0x82,
     0x2D,
     0x37,
     {0x05, 0x04},
     {0x21, 0x6F, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Ponyta",
     &I_ponyta,
     0xA3,
     0x32,
     0x55,
     0x37,
     0x5A,
     0x41,
     {0x14, 0x14},
     {0x34, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Rapidash",
     &I_rapidash,
     0xA4,
     0x41,
     0x64,
     0x46,
     0x69,
     0x50,
     {0x14, 0x14},
     {0x34, 0x27, 0x17, 0x2D},
     GROWTH_MEDIUM_FAST},
    {"Slowpoke",
     &I_slowpoke,
     0x25,
     0x5A,
     0x41,
     0x41,
     0x0F,
     0x28,
     {0x15, 0x18},
     {0x5D, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Slowbro",
     &I_slowbro,
     0x08,
     0x5F,
     0x4B,
     0x6E,
     0x1E,
     0x50,
     {0x15, 0x18},
     {0x5D, 0x32, 0x1D, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Magnemite",
     &I_magnemite,
     0xAD,
     0x19,
     0x23,
     0x46,
     0x2D,
     0x5F,
     {0x17, 0x17},
     {0x21, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Magneton",
     &I_magneton,
     0x36,
     0x32,
     0x3C,
     0x5F,
     0x46,
     0x78,
     {0x17, 0x17},
     {0x21, 0x31, 0x54, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Farfetch'd",
     &I_farfetchd,
     0x40,
     0x34,
     0x41,
     0x37,
     0x3C,
     0x3A,
     {0x00, 0x02},
     {0x40, 0x1C, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Doduo",
     &I_doduo,
     0x46,
     0x23,
     0x55,
     0x2D,
     0x4B,
     0x23,
     {0x00, 0x02},
     {0x40, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Dodrio",
     &I_dodrio,
     0x74,
     0x3C,
     0x6E,
     0x46,
     0x64,
     0x3C,
     {0x00, 0x02},
     {0x40, 0x2D, 0x1F, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Seel",
     &I_seel,
     0x3A,
     0x41,
     0x2D,
     0x37,
     0x2D,
     0x46,
     {0x15, 0x15},
     {0x1D, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Dewgong",
     &I_dewgong,
     0x78,
     0x5A,
     0x46,
     0x50,
     0x46,
     0x5F,
     {0x15, 0x19},
     {0x1D, 0x2D, 0x3E, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Grimer",
     &I_grimer,
     0x0D,
     0x50,
     0x50,
     0x32,
     0x19,
     0x28,
     {0x03, 0x03},
     {0x01, 0x32, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Muk",
     &I_muk,
     0x88,
     0x69,
     0x69,
     0x4B,
     0x32,
     0x41,
     {0x03, 0x03},
     {0x01, 0x32, 0x8B, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Shellder",
     &I_shellder,
     0x17,
     0x1E,
     0x41,
     0x64,
     0x28,
     0x2D,
     {0x15, 0x15},
     {0x21, 0x6E, 0x00, 0x00},
     GROWTH_SLOW},
    {"Cloyster",
     &I_cloyster,
     0x8B,
     0x32,
     0x5F,
     0xB4,
     0x46,
     0x55,
     {0x15, 0x19},
     {0x6E, 0x30, 0x80, 0x3E},
     GROWTH_SLOW},
    {"Gastly",
     &I_gastly,
     0x19,
     0x1E,
     0x23,
     0x1E,
     0x50,
     0x64,
     {0x08, 0x03},
     {0x7A, 0x6D, 0x65, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Haunter",
     &I_haunter,
     0x93,
     0x2D,
     0x32,
     0x2D,
     0x5F,
     0x73,
     {0x08, 0x03},
     {0x7A, 0x6D, 0x65, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Gengar",
     &I_gengar,
     0x0E,
     0x3C,
     0x41,
     0x3C,
     0x6E,
     0x82,
     {0x08, 0x03},
     {0x7A, 0x6D, 0x65, 0x00},
     GROWTH_MEDIUM_SLOW},
    {"Onix",
     &I_onix,
     0x22,
     0x23,
     0x2D,
     0xA0,
     0x46,
     0x1E,
     {0x05, 0x04},
     {0x21, 0x67, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Drowzee",
     &I_drowzee,
     0x30,
     0x3C,
     0x30,
     0x2D,
     0x2A,
     0x5A,
     {0x18, 0x18},
     {0x01, 0x5F, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Hypno",
     &I_hypno,
     0x81,
     0x55,
     0x49,
     0x46,
     0x43,
     0x73,
     {0x18, 0x18},
     {0x01, 0x5F, 0x32, 0x5D},
     GROWTH_MEDIUM_FAST},
    {"Krabby",
     &I_krabby,
     0x4E,
     0x1E,
     0x69,
     0x5A,
     0x32,
     0x19,
     {0x15, 0x15},
     {0x91, 0x2B, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Kingler",
     &I_kingler,
     0x8A,
     0x37,
     0x82,
     0x73,
     0x4B,
     0x32,
     {0x15, 0x15},
     {0x91, 0x2B, 0x0B, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Voltorb",
     &I_voltorb,
     0x06,
     0x28,
     0x1E,
     0x32,
     0x64,
     0x37,
     {0x17, 0x17},
     {0x21, 0x67, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Electrode",
     &I_electrode,
     0x8D,
     0x3C,
     0x32,
     0x46,
     0x8C,
     0x50,
     {0x17, 0x17},
     {0x21, 0x67, 0x31, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Exeggcute",
     &I_exeggcute,
     0x0C,
     0x3C,
     0x28,
     0x50,
     0x28,
     0x3C,
     {0x16, 0x18},
     {0x8C, 0x5F, 0x00, 0x00},
     GROWTH_SLOW},
    {"Exeggutor",
     &I_exeggutor,
     0x0A,
     0x5F,
     0x5F,
     0x55,
     0x37,
     0x7D,
     {0x16, 0x18},
     {0x8C, 0x5F, 0x00, 0x00},
     GROWTH_SLOW},
    {"Cubone",
     &I_cubone,
     0x11,
     0x32,
     0x32,
     0x5F,
     0x23,
     0x28,
     {0x04, 0x04},
     {0x7D, 0x2D, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Marowak",
     &I_marowak,
     0x91,
     0x3C,
     0x50,
     0x6E,
     0x2D,
     0x32,
     {0x04, 0x04},
     {0x7D, 0x2D, 0x2B, 0x74},
     GROWTH_MEDIUM_FAST},
    {"Hitmonlee",
     &I_hitmonlee,
     0x2B,
     0x32,
     0x78,
     0x35,
     0x57,
     0x23,
     {0x01, 0x01},
     {0x18, 0x60, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Hitmonchan",
     &I_hitmonchan,
     0x2C,
     0x32,
     0x69,
     0x4F,
     0x4C,
     0x23,
     {0x01, 0x01},
     {0x04, 0x61, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Lickitung",
     &I_lickitung,
     0x0B,
     0x5A,
     0x37,
     0x4B,
     0x1E,
     0x3C,
     {0x00, 0x00},
     {0x23, 0x30, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Koffing",
     &I_koffing,
     0x37,
     0x28,
     0x41,
     0x5F,
     0x23,
     0x3C,
     {0x03, 0x03},
     {0x21, 0x7B, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Weezing",
     &I_weezing,
     0x8F,
     0x41,
     0x5A,
     0x78,
     0x3C,
     0x55,
     {0x03, 0x03},
     {0x21, 0x7B, 0x7C, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Rhyhorn",
     &I_rhyhorn,
     0x12,
     0x50,
     0x55,
     0x5F,
     0x19,
     0x1E,
     {0x04, 0x05},
     {0x1E, 0x00, 0x00, 0x00},
     GROWTH_SLOW},
    {"Rhydon",
     &I_rhydon,
     0x01,
     0x69,
     0x82,
     0x78,
     0x28,
     0x2D,
     {0x04, 0x05},
     {0x1E, 0x17, 0x27, 0x1F},
     GROWTH_SLOW},
    {"Chansey",
     &I_chansey,
     0x28,
     0xFA,
     0x05,
     0x05,
     0x32,
     0x69,
     {0x00, 0x00},
     {0x01, 0x03, 0x00, 0x00},
     GROWTH_FAST},
    {"Tangela",
     &I_tangela,
     0x1E,
     0x41,
     0x37,
     0x73,
     0x3C,
     0x64,
     {0x16, 0x16},
     {0x84, 0x14, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Kangaskhan",
     &I_kangaskhan,
     0x02,
     0x69,
     0x5F,
     0x50,
     0x5A,
     0x28,
     {0x00, 0x00},
     {0x04, 0x63, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Horsea",
     &I_horsea,
     0x5C,
     0x1E,
     0x28,
     0x46,
     0x3C,
     0x46,
     {0x15, 0x15},
     {0x91, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Seadra",
     &I_seadra,
     0x5D,
     0x37,
     0x41,
     0x5F,
     0x55,
     0x5F,
     {0x15, 0x15},
     {0x91, 0x6C, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Goldeen",
     &I_goldeen,
     0x9D,
     0x2D,
     0x43,
     0x3C,
     0x3F,
     0x32,
     {0x15, 0x15},
     {0x40, 0x27, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Seaking",
     &I_seaking,
     0x9E,
     0x50,
     0x5C,
     0x41,
     0x44,
     0x50,
     {0x15, 0x15},
     {0x40, 0x27, 0x30, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Staryu",
     &I_staryu,
     0x1B,
     0x1E,
     0x2D,
     0x37,
     0x55,
     0x46,
     {0x15, 0x15},
     {0x21, 0x00, 0x00, 0x00},
     GROWTH_SLOW},
    {"Starmie",
     &I_starmie,
     0x98,
     0x3C,
     0x4B,
     0x55,
     0x73,
     0x64,
     {0x15, 0x18},
     {0x21, 0x37, 0x6A, 0x00},
     GROWTH_SLOW},
    {"Mr.Mime",
     &I_mr_mime,
     0x2A,
     0x28,
     0x2D,
     0x41,
     0x5A,
     0x64,
     {0x18, 0x18},
     {0x5D, 0x70, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Scyther",
     &I_scyther,
     0x1A,
     0x46,
     0x6E,
     0x50,
     0x69,
     0x37,
     {0x07, 0x02},
     {0x62, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Jynx",
     &I_jynx,
     0x48,
     0x41,
     0x32,
     0x23,
     0x5F,
     0x5F,
     {0x19, 0x18},
     {0x01, 0x8E, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Electabuzz",
     &I_electabuzz,
     0x35,
     0x41,
     0x53,
     0x39,
     0x69,
     0x55,
     {0x17, 0x17},
     {0x62, 0x2B, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Magmar",
     &I_magmar,
     0x33,
     0x41,
     0x5F,
     0x39,
     0x5D,
     0x55,
     {0x14, 0x14},
     {0x34, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Pinsir",
     &I_pinsir,
     0x1D,
     0x41,
     0x7D,
     0x64,
     0x55,
     0x37,
     {0x07, 0x07},
     {0x0B, 0x00, 0x00, 0x00},
     GROWTH_SLOW},
    {"Tauros",
     &I_tauros,
     0x3C,
     0x4B,
     0x64,
     0x5F,
     0x6E,
     0x46,
     {0x00, 0x00},
     {0x21, 0x00, 0x00, 0x00},
     GROWTH_SLOW},
    {"Magikarp",
     &I_magikarp,
     0x85,
     0x14,
     0x0A,
     0x37,
     0x50,
     0x14,
     {0x15, 0x15},
     {0x96, 0x00, 0x00, 0x00},
     GROWTH_SLOW},
    {"Gyarados",
     &I_gyarados,
     0x16,
     0x5F,
     0x7D,
     0x4F,
     0x51,
     0x64,
     {0x15, 0x02},
     {0x2C, 0x52, 0x2B, 0x38},
     GROWTH_SLOW},
    {"Lapras",
     &I_lapras,
     0x13,
     0x82,
     0x55,
     0x50,
     0x3C,
     0x5F,
     {0x15, 0x19},
     {0x37, 0x2D, 0x00, 0x00},
     GROWTH_SLOW},
    {"Ditto",
     &I_ditto,
     0x4C,
     0x30,
     0x30,
     0x30,
     0x30,
     0x30,
     {0x00, 0x00},
     {0x90, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Eevee",
     &I_eevee,
     0x66,
     0x37,
     0x37,
     0x32,
     0x37,
     0x41,
     {0x00, 0x00},
     {0x21, 0x1C, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Vaporeon",
     &I_vaporeon,
     0x69,
     0x82,
     0x41,
     0x3C,
     0x41,
     0x6E,
     {0x15, 0x15},
     {0x21, 0x1C, 0x62, 0x37},
     GROWTH_MEDIUM_FAST},
    {"Jolteon",
     &I_jolteon,
     0x68,
     0x41,
     0x41,
     0x3C,
     0x82,
     0x6E,
     {0x17, 0x17},
     {0x21, 0x1C, 0x62, 0x54},
     GROWTH_MEDIUM_FAST},
    {"Flareon",
     &I_flareon,
     0x67,
     0x41,
     0x82,
     0x3C,
     0x41,
     0x6E,
     {0x14, 0x14},
     {0x21, 0x1C, 0x62, 0x34},
     GROWTH_MEDIUM_FAST},
    {"Porygon",
     &I_porygon,
     0xAA,
     0x41,
     0x3C,
     0x46,
     0x28,
     0x4B,
     {0x00, 0x00},
     {0x21, 0x9F, 0xA0, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Omanyte",
     &I_omanyte,
     0x62,
     0x23,
     0x28,
     0x64,
     0x23,
     0x5A,
     {0x05, 0x15},
     {0x37, 0x6E, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Omastar",
     &I_omastar,
     0x63,
     0x46,
     0x3C,
     0x7D,
     0x37,
     0x73,
     {0x05, 0x15},
     {0x37, 0x6E, 0x1E, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Kabuto",
     &I_kabuto,
     0x5A,
     0x1E,
     0x50,
     0x5A,
     0x37,
     0x2D,
     {0x05, 0x15},
     {0x0A, 0x6A, 0x00, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Kabutops",
     &I_kabutops,
     0x5B,
     0x3C,
     0x73,
     0x69,
     0x50,
     0x46,
     {0x05, 0x15},
     {0x0A, 0x6A, 0x47, 0x00},
     GROWTH_MEDIUM_FAST},
    {"Aerodactyl",
     &I_aerodactyl,
     0xAB,
     0x50,
     0x69,
     0x41,
     0x82,
     0x3C,
     {0x05, 0x02},
     {0x11, 0x61, 0x00, 0x00},
     GROWTH_SLOW},
    {"Snorlax",
     &I_snorlax,
     0x84,
     0xA0,
     0x6E,
     0x41,
     0x1E,
     0x41,
     {0x00, 0x00},
     {0x1D, 0x85, 0x9C, 0x00},
     GROWTH_SLOW},
    {"Articuno",
     &I_articuno,
     0x4A,
     0x5A,
     0x55,
     0x64,
     0x55,
     0x7D,
     {0x19, 0x02},
     {0x40, 0x3A, 0x00, 0x00},
     GROWTH_SLOW},
    {"Zapdos",
     &I_zapdos,
     0x4B,
     0x5A,
     0x5A,
     0x55,
     0x64,
     0x7D,
     {0x17, 0x02},
     {0x54, 0x41, 0x00, 0x00},
     GROWTH_SLOW},
    {"Moltres",
     &I_moltres,
     0x49,
     0x5A,
     0x64,
     0x5A,
     0x5A,
     0x7D,
     {0x14, 0x02},
     {0x40, 0x53, 0x00, 0x00},
     GROWTH_SLOW},
    {"Dratini",
     &I_dratini,
     0x58,
     0x29,
     0x40,
     0x2D,
     0x32,
     0x32,
     {0x1A, 0x1A},
     {0x23, 0x2B, 0x00, 0x00},
     GROWTH_SLOW},
    {"Dragonair",
     &I_dragonair,
     0x59,
     0x3D,
     0x54,
     0x41,
     0x46,
     0x46,
     {0x1A, 0x1A},
     {0x23, 0x2B, 0x56, 0x00},
     GROWTH_SLOW},
    {"Dragonite",
     &I_dragonite,
     0x42,
     0x5B,
     0x86,
     0x5F,
     0x50,
     0x64,
     {0x1A, 0x02},
     {0x23, 0x2B, 0x56, 0x61},
     GROWTH_SLOW},
    {"Mewtwo",
     &I_mewtwo,
     0x83,
     0x6A,
     0x6E,
     0x5A,
     0x82,
     0x9A,
     {0x18, 0x18},
     {0x5D, 0x32, 0x81, 0x5E},
     GROWTH_SLOW},
    {"Mew",
     &I_mew,
     0x15,
     0x64,
     0x64,
     0x64,
     0x64,
     0x64,
     {0x18, 0x18},
     {0x01, 0x00, 0x00, 0x00},
     GROWTH_MEDIUM_SLOW},
    {},
};

const NamedList move_list[] = {
    {"No Move", 0x00, GEN_I},
    {"Absorb", 0x47, GEN_I},
    {"Acid Armor", 0x97, GEN_I},
    {"Acid", 0x33, GEN_I},
    {"Agility", 0x61, GEN_I},
    {"Amnesia", 0x85, GEN_I},
    {"Aurora Beam", 0x3E, GEN_I},
    {"Barrage", 0x8C, GEN_I},
    {"Barrier", 0x70, GEN_I},
    {"Bide", 0x75, GEN_I},
    {"Bind", 0x14, GEN_I},
    {"Bite", 0x2C, GEN_I},
    {"Blizzard", 0x3B, GEN_I},
    {"Body Slam", 0x22, GEN_I},
    {"Bone Club", 0x7D, GEN_I},
    {"Boomerang", 0x9B, GEN_I},
    {"Bubblebeam", 0x3D, GEN_I},
    {"Bubble", 0x91, GEN_I},
    {"Clamp", 0x80, GEN_I},
    {"Comet Punch", 0x04, GEN_I},
    {"Confuse Ray", 0x6D, GEN_I},
    {"Confusion", 0x5D, GEN_I},
    {"Constrict", 0x84, GEN_I},
    {"Conversion", 0xA0, GEN_I},
    {"Counter", 0x44, GEN_I},
    {"Crabhammer", 0x98, GEN_I},
    {"Cut", 0x0F, GEN_I},
    {"Defense Curl", 0x6F, GEN_I},
    {"Dig", 0x5B, GEN_I},
    {"Disable", 0x32, GEN_I},
    {"Dizzy Punch", 0x92, GEN_I},
    {"Doubleslap", 0x03, GEN_I},
    {"Double Kick", 0x18, GEN_I},
    {"Double Team", 0x68, GEN_I},
    {"Double-Edge", 0x26, GEN_I},
    {"Dragon Rage", 0x52, GEN_I},
    {"Dream Eater", 0x8A, GEN_I},
    {"Drill Peck", 0x41, GEN_I},
    {"Earthquake", 0x59, GEN_I},
    {"Egg Bomb", 0x79, GEN_I},
    {"Ember", 0x34, GEN_I},
    {"Explosion", 0x99, GEN_I},
    {"Fire Blast", 0x7E, GEN_I},
    {"Fire Punch", 0x07, GEN_I},
    {"Fire Spin", 0x53, GEN_I},
    {"Fissure", 0x5A, GEN_I},
    {"Flamethrower", 0x35, GEN_I},
    {"Flash", 0x94, GEN_I},
    {"Fly", 0x13, GEN_I},
    {"Focus Energy", 0x74, GEN_I},
    {"Fury Attack", 0x1F, GEN_I},
    {"Fury Swipes", 0x9A, GEN_I},
    {"Glare", 0x89, GEN_I},
    {"Growl", 0x2D, GEN_I},
    {"Growth", 0x4A, GEN_I},
    {"Guillotine", 0x0C, GEN_I},
    {"Gust", 0x10, GEN_I},
    {"Harden", 0x6A, GEN_I},
    {"Haze", 0x72, GEN_I},
    {"Headbutt", 0x1D, GEN_I},
    {"Hi Jump Kick", 0x88, GEN_I},
    {"Horn Attack", 0x1E, GEN_I},
    {"Horn Drill", 0x20, GEN_I},
    {"Hydro Pump", 0x38, GEN_I},
    {"Hyper Beam", 0x3F, GEN_I},
    {"Hyper Fang", 0x9E, GEN_I},
    {"Hypnosis", 0x5F, GEN_I},
    {"Ice Beam", 0x3A, GEN_I},
    {"Ice Punch", 0x08, GEN_I},
    {"Jump Kick", 0x1A, GEN_I},
    {"Karate Chop", 0x02, GEN_I},
    {"Kinesis", 0x86, GEN_I},
    {"Leech Life", 0x8D, GEN_I},
    {"Leech Seed", 0x49, GEN_I},
    {"Leer", 0x2B, GEN_I},
    {"Lick", 0x7A, GEN_I},
    {"Light Screen", 0x71, GEN_I},
    {"Lovely Kiss", 0x8E, GEN_I},
    {"Low Kick", 0x43, GEN_I},
    {"Meditate", 0x60, GEN_I},
    {"Mega Drain", 0x48, GEN_I},
    {"Mega Kick", 0x19, GEN_I},
    {"Mega Punch", 0x05, GEN_I},
    {"Metronome", 0x76, GEN_I},
    {"Mimic", 0x66, GEN_I},
    {"Minimize", 0x6B, GEN_I},
    {"Mirror Move", 0x77, GEN_I},
    {"Mist", 0x36, GEN_I},
    {"Night Shade", 0x65, GEN_I},
    {"Pay Day", 0x06, GEN_I},
    {"Peck", 0x40, GEN_I},
    {"Petal Dance", 0x50, GEN_I},
    {"Pin Missile", 0x2A, GEN_I},
    {"Poisonpowder", 0x4D, GEN_I},
    {"Poison Gas", 0x8B, GEN_I},
    {"Poison Sting", 0x28, GEN_I},
    {"Pound", 0x01, GEN_I},
    {"Psybeam", 0x3C, GEN_I},
    {"Psychic", 0x5E, GEN_I},
    {"Psywave", 0x95, GEN_I},
    {"Quick Attack", 0x62, GEN_I},
    {"Rage", 0x63, GEN_I},
    {"Razor Leaf", 0x4B, GEN_I},
    {"Razor Wind", 0x0D, GEN_I},
    {"Recover", 0x69, GEN_I},
    {"Reflect", 0x73, GEN_I},
    {"Rest", 0x9C, GEN_I},
    {"Roar", 0x2E, GEN_I},
    {"Rock Slide", 0x9D, GEN_I},
    {"Rock Throw", 0x58, GEN_I},
    {"Rolling Kick", 0x1B, GEN_I},
    {"Sand Attack", 0x1C, GEN_I},
    {"Scratch", 0x0A, GEN_I},
    {"Screech", 0x67, GEN_I},
    {"Seismic Toss", 0x45, GEN_I},
    {"Selfdestruct", 0x78, GEN_I},
    {"Sharpen", 0x9F, GEN_I},
    {"Sing", 0x2F, GEN_I},
    {"Skull Bash", 0x82, GEN_I},
    {"Sky Attack", 0x8F, GEN_I},
    {"Slam", 0x15, GEN_I},
    {"Slash", 0xA3, GEN_I},
    {"Sleep Powder", 0x4F, GEN_I},
    {"Sludge", 0x7C, GEN_I},
    {"Smog", 0x7B, GEN_I},
    {"Smokescreen", 0x6C, GEN_I},
    {"Softboiled", 0x87, GEN_I},
    {"Solar Beam", 0x4C, GEN_I},
    {"Sonicboom", 0x31, GEN_I},
    {"Spike Cannon", 0x83, GEN_I},
    {"Splash", 0x96, GEN_I},
    {"Spore", 0x93, GEN_I},
    {"Stomp", 0x17, GEN_I},
    {"Strength", 0x46, GEN_I},
    {"String Shot", 0x51, GEN_I},
    {"Struggle", 0xA5, GEN_I},
    {"Stun Spore", 0x4E, GEN_I},
    {"Submission", 0x42, GEN_I},
    {"Substitute", 0xA4, GEN_I},
    {"Supersonic", 0x30, GEN_I},
    {"Super Fang", 0xA2, GEN_I},
    {"Surf", 0x39, GEN_I},
    {"Swift", 0x81, GEN_I},
    {"Swords Dance", 0x0E, GEN_I},
    {"Tackle", 0x21, GEN_I},
    {"Tail Whip", 0x27, GEN_I},
    {"Take Down", 0x24, GEN_I},
    {"Teleport", 0x64, GEN_I},
    {"Thrash", 0x25, GEN_I},
    {"Thunderbolt", 0x55, GEN_I},
    {"Thunderpunch", 0x09, GEN_I},
    {"Thundershock", 0x54, GEN_I},
    {"Thunder Wave", 0x56, GEN_I},
    {"Thunder", 0x57, GEN_I},
    {"Toxic", 0x5C, GEN_I},
    {"Transform", 0x90, GEN_I},
    {"Tri Attack", 0xA1, GEN_I},
    {"Twineedle", 0x29, GEN_I},
    {"Vicegrip", 0x0B, GEN_I},
    {"Vine Whip", 0x16, GEN_I},
    {"Waterfall", 0x7F, GEN_I},
    {"Water Gun", 0x37, GEN_I},
    {"Whirlwind", 0x12, GEN_I},
    {"Wing Attack", 0x11, GEN_I},
    {"Withdraw", 0x6E, GEN_I},
    {"Wrap", 0x23, GEN_I},
    {},
};

const NamedList type_list[] = {
    {"Bug", 0x07, GEN_I},
    {"Dragon", 0x1A, GEN_I},
    {"Electric", 0x17, GEN_I},
    {"Fighting", 0x01, GEN_I},
    {"Fire", 0x14, GEN_I},
    {"Flying", 0x02, GEN_I},
    {"Ghost", 0x08, GEN_I},
    {"Grass", 0x16, GEN_I},
    {"Ground", 0x04, GEN_I},
    {"Ice", 0x19, GEN_I},
    {"Normal", 0x00, GEN_I},
    {"Poison", 0x03, GEN_I},
    {"Psychic", 0x18, GEN_I},
    {"Rock", 0x05, GEN_I},
    {"Water", 0x15, GEN_I},
    /* Types are not transferred in gen ii */
    {},
};

const NamedList stat_list[] = {
    {"Random IV, Zero EV", RANDIV_ZEROEV, 0},
    {"Random IV, Max EV / Level", RANDIV_LEVELEV, 0},
    {"Random IV, Max EV", RANDIV_MAXEV, 0},
    {"Max IV, Zero EV", MAXIV_ZEROEV, 0},
    {"Max IV, Max EV / Level", MAXIV_LEVELEV, 0},
    {"Max IV, Max EV", MAXIV_MAXEV, 0},
    {},
};
