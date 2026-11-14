/*
 * DISPATCH.C - proto_dispatch, the command table, the gate, and the
 * helpers shared by the command handlers.
 *
 * A request goes through these steps, each of which may end it with an
 * -ERR line:
 *
 *   1  the engine's error is cleared; a closed session runs nothing;
 *   2  the line is tokenized (ENG_ESYNTAX, ENG_EARG, ENG_ETOOLONG); a
 *      line of only spaces and tabs is no request and gets no response;
 *   3  the command is looked up (ENG_EUNKNOWN), before any check of the
 *      session, so an unknown command is ENG_EUNKNOWN in every state;
 *   4  the actor (station and user) is handed to the engine and the gate
 *      checks that the data files are open, the session state, the
 *      user's level, including the level an ADD= pair needs, and that a
 *      command needing the system to itself has it (ENG_EIO,
 *      ENG_ENOLOGIN, ENG_EMUSTCHG, ENG_EPERM, ENG_EBUSY);
 *   5  the handler runs: the host's ticket command (proto_ticket_cmd)
 *      for LOGIN, PASSWD and BYE when the host has a login table, else
 *      the command's own.  It writes the whole response and returns
 *      ENG_OK, or returns an ENG_E code having written nothing, and then
 *      the -ERR line is written here.
 *
 * Passwords travel in LOGIN, PASSWD, USER ADD and USER RESET, login
 * tickets in LOGIN and in the answers of LOGIN and PASSWD.  After every
 * request the tokenized copy of the line (proto_req.buf) is overwritten,
 * so no password or ticket stays on the stack.  The line itself belongs
 * to the caller: a transport must not keep copies of it either.  A
 * transport that assembles lines with proto_acc calls proto_acc_wipe
 * once proto_dispatch has returned (the assembler also wipes its buffer
 * at the next byte and when it discards an over-long line); one that
 * holds request lines elsewhere (a receive buffer) clears them itself
 * at that point.  A transport whose sessions are offered tickets wipes
 * the proto_out then too, as its line keeps the response's last line.
 *
 * Every transport goes through proto_dispatch, so the level checks here
 * apply to all of them.  The effective level the gate compares with was
 * fixed at LOGIN by SESSION.C, already capped for a read-only station.
 *
 * While the handler runs, a sink is wrapped around the output's that
 * notes whether it writes anything.  A handler (or a helper below) that
 * fails after it has begun a +DATA block ends that block itself with
 * "K ERROR=code" and may still return the code; the note shows that a
 * response was written, so no second one is added and every request
 * gets exactly one.  A +DATA block a handler leaves open is closed here
 * with "K ERROR=" and its code (ENG_EINTERNAL if it returned ENG_OK).
 */
#define PROTO_TICKETS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGNOTE.H"

/* ---------------------------------------------------------------------
 * The command table.
 *
 * Levels follow the users' access levels: ENG_LVL_READ for reading,
 * ENG_LVL_OPER for changes to items, locations, categories, location
 * types, assemblies, maintenance records and the LOAN group (loans,
 * reservations, in-use assignments), ENG_LVL_ADMIN for user accounts,
 * the audit list, EXPORT and IMPORT, which write and read files on the
 * machine running the engine, and the ADMIN group.  The REPORT group
 * (ITEMS, LOANS) reads at ENG_LVL_READ.  ITEM NOTES, LOC NOTES and MAINT
 * NOTES read at ENG_LVL_READ and write, with a pair (ADD=, NEW=, PART=
 * or END=), at ENG_LVL_OPER (PROTO_C_KEYOPER).  IMPORT and ADMIN PACK,
 * REINDEX, BACKUP and RESTORE, whose work is not bounded per request,
 * need the system to themselves (PROTO_C_EXCL): once the level is met
 * they are ENG_EBUSY while other sessions are open.  LOAN ADD is another
 * name for LOAN OUT.
 *
 * A command is its verb, and for group commands (USER ITEM CAT LTYPE LOC
 * ASSY MAINT LOAN REPORT AUDIT ADMIN) the next word, matched
 * case-insensitively.  An entry with sub NULL (single-word commands such
 * as LOGIN, EXPORT and IMPORT) matches whatever follows the verb.  The
 * lookup runs first, so an unknown command is ENG_EUNKNOWN in every
 * state; then the gate (proto_gate), then the handler.
 *
 * An entry names its verb by number (VB_), so that each entry takes
 * one byte for it, and the lookup compares the verb with the words of
 * verbs[] once instead of with every entry's.  The second words of more
 * than one entry are kept once each below: a compiler that does not
 * merge equal string literals would store a copy per entry.
 * --------------------------------------------------------------------- */
#define PROTO_C_LEVEL   0x03    /* the ENG_LVL_ needed, 0 for none      */
#define PROTO_C_ANY     0x04    /* allowed in every open state          */
#define PROTO_C_HELLOED 0x08    /* needs HELLO, not LOGIN               */
#define PROTO_C_MUSTCHG 0x10    /* allowed while MUSTCHG is pending     */
#define PROTO_C_KEYOPER 0x20    /* a pair needs ENG_LVL_OPER            */
#define PROTO_C_EXCL    0x40    /* needs the system to itself           */

enum {
    VB_HELLO, VB_BYE, VB_PING, VB_VER, VB_LOGIN, VB_PASSWD, VB_WHOAMI,
    VB_STATUS, VB_USER, VB_ITEM, VB_CAT, VB_LTYPE, VB_LOC, VB_ASSY,
    VB_MAINT, VB_LOAN, VB_REPORT, VB_EXPORT, VB_IMPORT, VB_AUDIT, VB_ADMIN,
    VB_COUNT
};

/* The verbs, in the order of their VB_ numbers. */
static const char *const verbs[] = {
    "HELLO", "BYE", "PING", "VER", "LOGIN", "PASSWD", "WHOAMI",
    "STATUS", "USER", "ITEM", "CAT", "LTYPE", "LOC", "ASSY",
    "MAINT", "LOAN", "REPORT", "EXPORT", "IMPORT", "AUDIT", "ADMIN"
};

/* verbs[] holds one word for each VB_ number. */
extern char fits_verbs[1];
extern char fits_verbs[(sizeof verbs / sizeof verbs[0] == VB_COUNT) ?
                       1 : 2];

/* The two u8 members are kept together, so that no compiler pads the
   entry. */
typedef struct {
    u8 verb;                    /* VB_                                  */
    u8 gate;                    /* level and PROTO_C_ flags             */
    const char *sub;            /* NULL for a one-word command          */
    proto_cmd_fn fn;
} proto_cmd;

/* The levels must fit the PROTO_C_LEVEL bits. */
extern char fits_level[1];
extern char fits_level[(ENG_LVL_ADMIN <= PROTO_C_LEVEL) ? 1 : 2];

/* The engine sizes its arrays of a request's pairs by ENG_MAXPAIRS; the
   tokenizer admits up to PROTO_MAXPAIRS of them. */
extern char fits_pairs[1];
extern char fits_pairs[(ENG_MAXPAIRS == PROTO_MAXPAIRS) ? 1 : 2];

#define C_ANY           PROTO_C_ANY
#define C_HELLOED       PROTO_C_HELLOED
#define C_MUSTCHG       PROTO_C_MUSTCHG
#define C_KEYOPER       PROTO_C_KEYOPER
#define C_EXCL          PROTO_C_EXCL
#define C_L1            ENG_LVL_READ
#define C_L2            ENG_LVL_OPER
#define C_L3            ENG_LVL_ADMIN

static const char w_list[] = "LIST";
static const char w_get[] = "GET";
static const char w_add[] = "ADD";
static const char w_set[] = "SET";
static const char w_del[] = "DEL";
static const char w_notes[] = "NOTES";

static const proto_cmd cmds[] = {
    { VB_HELLO,  C_ANY,            NULL,       dp_hello },
    { VB_BYE,    C_ANY,            NULL,       dp_bye },
    { VB_PING,   C_ANY,            NULL,       dp_ping },
    { VB_VER,    C_ANY,            NULL,       dp_ver },
    { VB_LOGIN,  C_HELLOED,        NULL,       dp_login },
    { VB_PASSWD, C_MUSTCHG | C_L1, NULL,       dp_passwd },
    { VB_WHOAMI, C_L1,             NULL,       dp_whoami },
    { VB_STATUS, C_L1,             NULL,       dp_status },

    { VB_USER,   C_L3,             w_list,     dp_user_list },
    { VB_USER,   C_L3,             w_get,      dp_user_get },
    { VB_USER,   C_L3,             w_add,      dp_user_add },
    { VB_USER,   C_L3,             w_set,      dp_user_set },
    { VB_USER,   C_L3,             "RESET",    dp_user_reset },
    { VB_USER,   C_L3,             w_del,      dp_user_del },

    { VB_ITEM,   C_L1,             w_get,      dp_item_get },
    { VB_ITEM,   C_L1,             w_list,     dp_item_list },
    { VB_ITEM,   C_L1,             "FIND",     dp_item_find },
    { VB_ITEM,   C_L1,             "SERIAL",   dp_item_serial },
    { VB_ITEM,   C_KEYOPER | C_L1, w_notes,    dp_item_notes },
    { VB_ITEM,   C_L2,             w_add,      dp_item_add },
    { VB_ITEM,   C_L2,             w_set,      dp_item_set },
    { VB_ITEM,   C_L2,             w_del,      dp_item_del },
    { VB_ITEM,   C_L2,             "UNDEL",    dp_item_undel },
    { VB_ITEM,   C_L2,             "COPY",     dp_item_copy },
    { VB_ITEM,   C_L2,             "SPLIT",    dp_item_split },
    { VB_ITEM,   C_L1,             "TAGS",     dp_item_tags },

    { VB_CAT,    C_L1,             w_list,     dp_cat_list },
    { VB_CAT,    C_L2,             w_add,      dp_cat_add },
    { VB_CAT,    C_L2,             w_set,      dp_cat_set },
    { VB_CAT,    C_L2,             w_del,      dp_cat_del },

    { VB_LTYPE,  C_L1,             w_list,     dp_ltype_list },
    { VB_LTYPE,  C_L2,             w_add,      dp_ltype_add },
    { VB_LTYPE,  C_L2,             w_set,      dp_ltype_set },
    { VB_LTYPE,  C_L2,             w_del,      dp_ltype_del },

    { VB_LOC,    C_L1,             w_get,      dp_loc_get },
    { VB_LOC,    C_L1,             w_list,     dp_loc_list },
    { VB_LOC,    C_L1,             "CONTENTS", dp_loc_contents },
    { VB_LOC,    C_KEYOPER | C_L1, w_notes,    dp_loc_notes },
    { VB_LOC,    C_L2,             w_add,      dp_loc_add },
    { VB_LOC,    C_L2,             w_set,      dp_loc_set },
    { VB_LOC,    C_L2,             w_del,      dp_loc_del },

    { VB_ASSY,   C_L1,             w_list,     dp_assy_list },
    { VB_ASSY,   C_L1,             "BOM",      dp_assy_bom },
    { VB_ASSY,   C_L1,             "PATH",     dp_assy_path },
    { VB_ASSY,   C_L1,             "CHILDREN", dp_assy_children },
    { VB_ASSY,   C_L2,             "INSTALL",  dp_assy_install },
    { VB_ASSY,   C_L2,             "REMOVE",   dp_assy_remove },
    { VB_ASSY,   C_L2,             "MOVE",     dp_assy_move },

    { VB_MAINT,  C_L1,             w_get,      dp_maint_get },
    { VB_MAINT,  C_L1,             w_list,     dp_maint_list },
    { VB_MAINT,  C_L1,             "RECENT",   dp_maint_recent },
    { VB_MAINT,  C_KEYOPER | C_L1, w_notes,    dp_maint_notes },
    { VB_MAINT,  C_L2,             w_add,      dp_maint_add },
    { VB_MAINT,  C_L2,             w_set,      dp_maint_set },
    { VB_MAINT,  C_L2,             w_del,      dp_maint_del },

    { VB_LOAN,   C_L1,             w_get,      dp_loan_get },
    { VB_LOAN,   C_L1,             w_list,     dp_loan_list },
    { VB_LOAN,   C_L1,             "OVERDUE",  dp_loan_overdue },
    { VB_LOAN,   C_L2,             "OUT",      dp_loan_out },
    { VB_LOAN,   C_L2,             w_add,      dp_loan_out },
    { VB_LOAN,   C_L2,             "RESERVE",  dp_loan_reserve },
    { VB_LOAN,   C_L2,             "USE",      dp_loan_use },
    { VB_LOAN,   C_L2,             "RETURN",   dp_loan_return },
    { VB_LOAN,   C_L2,             w_set,      dp_loan_set },

    { VB_REPORT, C_L1,             "ITEMS",    dp_report_items },
    { VB_REPORT, C_L1,             "LOANS",    dp_report_loans },

    { VB_EXPORT, C_L3,             NULL,       dp_export },
    { VB_IMPORT, C_EXCL | C_L3,    NULL,       dp_import },

    { VB_AUDIT,  C_L3,             w_list,     dp_audit_list },

    { VB_ADMIN,  C_L3,             "CLIENTS",  dp_admin_clients },
    { VB_ADMIN,  C_L3,             "FLUSH",    dp_admin_flush },
    { VB_ADMIN,  C_L3,             "SHUTDOWN", dp_admin_shutdown },
    { VB_ADMIN,  C_EXCL | C_L3,    "PACK",     dp_admin_pack },
    { VB_ADMIN,  C_EXCL | C_L3,    "REINDEX",  dp_admin_reindex },
    { VB_ADMIN,  C_EXCL | C_L3,    "BACKUP",   dp_admin_backup },
    { VB_ADMIN,  C_EXCL | C_L3,    "RESTORE",  dp_admin_restore }
};

#define NCMDS   (sizeof cmds / sizeof cmds[0])

/* The command for verb and sub (sub may be NULL), or NULL if none. */
static const proto_cmd *proto_lookup(const char *verb, const char *sub)
{
    u16 i;
    u8 v;

    for (v = 0; v < VB_COUNT && !txt_ieq(verb, verbs[v]); v++)
        ;
    for (i = 0; i < NCMDS; i++) {
        if (cmds[i].verb != v)
            continue;
        if (cmds[i].sub == NULL)
            return &cmds[i];
        if (sub != NULL && txt_ieq(sub, cmds[i].sub))
            return &cmds[i];
    }
    return NULL;
}

/* ENG_OK if the session s of host h may run cmd with the pairs of r,
   else, checked in this order: ENG_ENOLOGIN for a closed session;
   ENG_EIO for any command but the PROTO_C_ANY ones while the engine
   holds no file (an ADMIN command failed to open them again); then
   ENG_ENOLOGIN (no HELLO, or no LOGIN), ENG_EMUSTCHG, ENG_EPERM
   (effective level below the level in cmd->gate, or below ENG_LVL_OPER
   for a PROTO_C_KEYOPER command whose request has a pair),
   ENG_EBUSY (a PROTO_C_EXCL command while other sessions are open). */
static int proto_gate(const proto_host *h, const proto_session *s,
                      const proto_cmd *cmd, const proto_req *r)
{
    if (s->state == PROTO_S_CLOSED)
        return ENG_ENOLOGIN;
    if (cmd->gate & PROTO_C_ANY)
        return ENG_OK;
    if (!eng_is_open(h->e))
        return ENG_EIO;
    if (cmd->gate & PROTO_C_HELLOED)
        return s->state == PROTO_S_NEW ? ENG_ENOLOGIN : ENG_OK;
    if (s->state != PROTO_S_MUSTCHG && s->state != PROTO_S_READY)
        return ENG_ENOLOGIN;
    if (s->state == PROTO_S_MUSTCHG && !(cmd->gate & PROTO_C_MUSTCHG))
        return ENG_EMUSTCHG;
    if (!sess_level_ok(s, (u8)(cmd->gate & PROTO_C_LEVEL)))
        return ENG_EPERM;
    if ((cmd->gate & PROTO_C_KEYOPER) && r->npairs > 0 &&
        !sess_level_ok(s, ENG_LVL_OPER))
        return ENG_EPERM;
    if ((cmd->gate & PROTO_C_EXCL) && h->others != 0)
        return ENG_EBUSY;
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * The sink wrapped around the output while a handler runs.
 * --------------------------------------------------------------------- */
typedef struct {
    proto_sink below;           /* the output's own sink                */
    u8   wrote;                 /* the handler has written something    */
} note_sink;

static int note_write(void *arg, const char *buf, u16 len)
{
    note_sink *k;

    k = (note_sink *)arg;
    k->wrote = 1;
    return k->below.write(k->below.arg, buf, len);
}

static u16 note_room(void *arg)
{
    return proto_sink_room(&((note_sink *)arg)->below);
}

/* Run the handler c->cmd with a note_sink around o's sink; *wrote
   receives whether it wrote anything. */
static int run_handler(proto_call *c, int *wrote)
{
    note_sink k;
    int rc;

    k.wrote = 0;
    proto_sink_push(c->o, &k.below, note_write, note_room, &k);
    rc = c->cmd(c);
    proto_sink_pop(c->o, &k.below);
    *wrote = k.wrote;
    return rc;
}

/* The -ERR line of a handler's failure: the field and text recorded by
   proto_fail (whose text is the standard one of its code when it was
   given none), or else those of the engine, or else the standard text. */
static void handler_error(proto_call *c, int rc)
{
    const char *text;

    if (c->errmsg != NULL) {
        proto_err(c->o, rc, c->errmsg, c->errfld);
        return;
    }
    text = eng_errmsg(c->h->e);
    proto_err(c->o, rc, text[0] != '\0' ? text : NULL,
              eng_errfield(c->h->e));
}

/* Steps 2 to 5 of a request, with line tokenized into r; returns a
   PROTO_R_ code. */
static int run_request(proto_host *h, proto_session *s, const char *line,
                       u16 len, proto_out *o, proto_req *r)
{
    proto_call c;
    const proto_cmd *cmd;
    int rc;
    int wrote;

    rc = proto_parse(r, line, len);
    if (rc != ENG_OK) {
        proto_err(o, rc, NULL, rc == ENG_EARG ? r->bad : NULL);
        return PROTO_R_OK;
    }
    if (r->nargs == 0) {
        if (r->npairs == 0)
            return PROTO_R_NONE;
        proto_err(o, ENG_ESYNTAX, "no command", NULL);
        return PROTO_R_OK;
    }

    cmd = proto_lookup(r->arg[0], r->nargs > 1 ? r->arg[1] : NULL);
    if (cmd == NULL) {
        proto_err(o, ENG_EUNKNOWN, NULL, NULL);
        return PROTO_R_OK;
    }

    eng_set_actor(h->e, s->station, s->user);
    rc = proto_gate(h, s, cmd, r);
    if (rc != ENG_OK) {
        proto_err(o, rc, NULL, NULL);
        return PROTO_R_OK;
    }

    c.h = h;
    c.s = s;
    c.r = r;
    c.o = o;
    c.cmd = h->tickets != NULL && (cmd->verb == VB_LOGIN ||
            cmd->verb == VB_PASSWD || cmd->verb == VB_BYE) ?
            h->tickets->cmd : cmd->fn;
    c.argi = (u16)(cmd->sub != NULL ? 2 : 1);
    c.close = 0;
    c.errfld[0] = '\0';
    c.errmsg = NULL;
    rc = run_handler(&c, &wrote);
    if (o->indata)
        proto_err(o, rc != ENG_OK ? rc : ENG_EINTERNAL, NULL, NULL);
    else if (!wrote) {
        if (rc != ENG_OK)
            handler_error(&c, rc);
        else
            proto_err(o, ENG_EINTERNAL, "no response", NULL);
    }
    if (c.close || s->state == PROTO_S_CLOSED)
        return PROTO_R_CLOSE;
    return PROTO_R_OK;
}

int proto_dispatch(proto_host *h, proto_session *s, const char *line,
                   u16 len, proto_out *o)
{
    proto_req r;
    int res;

    eng_clear_error(h->e);
    if (s->state == PROTO_S_CLOSED)
        return PROTO_R_CLOSE;
    res = run_request(h, s, line, len, o, &r);
    txt_wipe(r.buf, (u16)sizeof r.buf);
    return res;
}

/* ---------------------------------------------------------------------
 * Arguments.
 * --------------------------------------------------------------------- */
static const char k_missing[] = "missing argument";
static const char k_unexpected[] = "unexpected key";
static const char k_badid[] = "bad ID";
static const char k_page[] = "PAGE";

int proto_fail(proto_call *c, int code, const char *field, const char *msg)
{
    u16 i;

    i = 0;
    if (field != NULL)
        for (; i < DBF_NAMELEN && field[i] != '\0'; i++)
            c->errfld[i] = field[i];
    c->errfld[i] = '\0';
    c->errmsg = msg != NULL ? msg : proto_errtext(code);
    return code;
}

const char *proto_arg(const proto_call *c, u16 i)
{
    u16 n;

    n = (u16)(c->argi + i);
    if (n < c->argi || n >= c->r->nargs)
        return NULL;
    return c->r->arg[n];
}

int proto_need(proto_call *c, u16 i, const char *field, const char **val)
{
    *val = proto_arg(c, i);
    if (*val == NULL)
        return proto_fail(c, ENG_EARG, field, k_missing);
    return ENG_OK;
}

int proto_no_more(proto_call *c, u16 n)
{
    if (proto_arg(c, n) != NULL)
        return proto_fail(c, ENG_ESYNTAX, NULL, "too many arguments");
    return ENG_OK;
}

int proto_args_end(proto_call *c, u16 n, const char *keys)
{
    int rc;

    rc = proto_no_more(c, n);
    if (rc == ENG_OK && keys != NULL)
        rc = proto_only_keys(c, keys);
    return rc;
}

int proto_need_key(proto_call *c, const char *key, const char **val)
{
    *val = proto_get(c->r, key);
    if (*val == NULL)
        return proto_fail(c, ENG_EARG, key, k_missing);
    return ENG_OK;
}

/* 1 if p is a record ID, decimal digits of a value of 1..ENG_MAXID,
   which goes into *id (0 otherwise). */
static int parse_id(const char *p, u32 *id)
{
    u32 v;

    *id = 0;
    if (*p == '\0')
        return 0;
    for (v = 0; *p != '\0'; p++) {
        if (!TXT_ISDIGIT(*p))
            return 0;
        v = v * 10 + (u32)(*p - '0');
        if (v > ENG_MAXID)
            return 0;
    }
    if (v == 0)
        return 0;
    *id = v;
    return 1;
}

int proto_need_id(proto_call *c, u16 i, u32 *id)
{
    return proto_need_idf(c, i, "ID", id);
}

int proto_need_idf(proto_call *c, u16 i, const char *field, u32 *id)
{
    const char *p;

    *id = 0;
    p = proto_arg(c, i);
    if (p == NULL)
        return proto_fail(c, ENG_EARG, field, k_missing);
    if (!parse_id(p, id))
        return proto_fail(c, ENG_EARG, field, k_badid);
    return ENG_OK;
}

int proto_key_id(proto_call *c, const char *key, u32 *id)
{
    const char *p;

    *id = 0;
    p = proto_get(c->r, key);
    if (p == NULL || *p == '\0')
        return ENG_OK;
    if (!parse_id(p, id))
        return proto_fail(c, ENG_EARG, key, k_badid);
    return ENG_OK;
}

int proto_flag(proto_call *c, const char *key, int *on)
{
    const char *p;

    *on = 0;
    p = proto_get(c->r, key);
    if (p == NULL || p[0] == '\0')
        return ENG_OK;
    if ((p[0] != '0' && p[0] != '1') || p[1] != '\0')
        return proto_fail(c, ENG_EARG, key, NULL);
    *on = p[0] == '1';
    return ENG_OK;
}

int proto_only_keys(proto_call *c, const char *keys)
{
    u16 i;

    for (i = 0; i < c->r->npairs; i++)
        if (!proto_in_list(keys, c->r->pair[i].key))
            return proto_fail(c, ENG_EARG, c->r->pair[i].key,
                              k_unexpected);
    return ENG_OK;
}

/* The keys of a NOTES write that stage, word k having how NOTE_HOW(k):
   NEW=, PART=, and END=, a PART= that ends the stage. */
static const char note_words[] = "NEW PART END";
#define NOTE_HOW(k)     ((k) == 0 ? ENG_NOTE_NEW : (k) == 1 ? ENG_NOTE_PART : \
                         ENG_NOTE_PART | ENG_NOTE_END)

/* A session's slot, at most PROTO_MAXPEERS, is a stage's tag too. */
extern char fits_note_tag[1];
extern char fits_note_tag[(PROTO_MAXPEERS <= ENG_NOTE_TAG) ? 1 : 2];

int proto_self(const proto_host *h, proto_peer *p)
{
    u16 i;

    if (h->peers != NULL)
        for (i = 0; i < PROTO_MAXPEERS && h->peers(h->hostarg, i, p) == 0;
             i++)
            if (p->self)
                return 1;
    return 0;
}

int proto_note(proto_call *c, const char **text, u8 *how)
{
    proto_peer me;
    const eng_pair *p;
    u16 i;
    int k;

    *text = NULL;
    *how = ENG_NOTE_ADD;
    for (i = 0; i < c->r->npairs; i++) {
        p = &c->r->pair[i];
        k = txt_word_num(note_words, p->key);
        if (k < 0 && strcmp(p->key, w_add) != 0)
            return proto_fail(c, ENG_EARG, p->key, k_unexpected);
        if (*text != NULL)
            return proto_fail(c, ENG_EARG, p->key, NULL);
        *text = p->val;
        *how = (u8)(k < 0 ? ENG_NOTE_ADD : NOTE_HOW(k));
    }
    if (*how != ENG_NOTE_ADD && !c->s->tkok && proto_self(c->h, &me))
        *how = (u8)(*how | me.slot);
    return ENG_OK;
}

int proto_page_size(proto_call *c, u16 *page)
{
    const char *p;
    u16 v;

    *page = PROTO_PAGEDEF;
    p = proto_get(c->r, k_page);
    if (p == NULL)
        return ENG_OK;
    for (v = 0; TXT_ISDIGIT(*p) && v <= PROTO_PAGEMAX; p++)
        v = (u16)(v * 10 + (u16)(*p - '0'));
    if (*p != '\0' || v == 0 || v > PROTO_PAGEMAX)
        return proto_fail(c, ENG_EARG, k_page, "bad page size");
    *page = v;
    return ENG_OK;
}

/* The computed column of table t named by the len bytes at p (any
   case), or -1. */
static int xcol_find(int t, const char *p, u16 len)
{
    int k;

    for (k = 0; k < DP_NXCOL; k++)
        if (dp_xcols[k].t == t && txt_ieqn(p, dp_xcols[k].name, len) &&
            dp_xcols[k].name[len] == '\0')
            return k;
    return -1;
}

/* The FIELDS= list spec of table t, as proto_page_args gives it.  Each
   computed column named is replaced by the field it follows in the
   default list, so eng_fieldlist checks the whole list, names and
   separators as they are, and then takes that place again; no name is
   shorter than its field's (DPLOC.C checks), so the list does not grow.
   Without a spec, the default list gets each computed column of t after
   its field. */
static int field_list(proto_call *c, int t, const char *spec, u16 *idx,
                      u16 *n)
{
    char rest[ENG_MAXFSPEC + 1];
    u16 at[DP_NXCOL];
    u16 xk[DP_NXCOL];
    const char *p;
    const char *w;
    eng *e;
    u16 seen;
    u16 len;
    u16 nx;
    u16 r;
    u16 i;
    int k;
    int rc;

    e = c->h->e;
    nx = 0;
    seen = 0;
    r = 0;
    p = spec != NULL && strlen(spec) <= ENG_MAXFSPEC ? spec : "";
    for (i = 0; *p != '\0'; i++) {
        for (len = 0; p[len] != '\0' && p[len] != ','; len++)
            ;
        w = p;
        p += len;
        k = xcol_find(t, w, len);
        if (k >= 0) {
            if (seen & (1u << k))
                return proto_fail(c, ENG_EARG, dp_xcols[k].name,
                                  proto_errtext(ENG_EDUP));
            seen = (u16)(seen | (1u << k));
            at[nx] = i;
            xk[nx++] = (u16)k;
            w = eng_field(e, t, dp_xcols[k].after)->name;
            len = (u16)strlen(w);
        }
        memcpy(rest + r, w, len);
        r = (u16)(r + len);
        if (*p == ',')
            rest[r++] = *p++;
    }
    rest[r] = '\0';
    *n = 0;
    rc = eng_fieldlist(e, t, nx > 0 ? rest : spec, idx, n);
    for (i = 0; rc == ENG_OK && i < nx; i++)
        idx[at[i]] = (u16)(DP_XCOL | xk[i]);
    if (spec == NULL || spec[0] == '\0')
        for (k = 0; rc == ENG_OK && k < DP_NXCOL; k++)
            for (i = 0; i < *n && dp_xcols[k].t == t; i++)
                if (idx[i] == dp_xcols[k].after) {
                    memmove(idx + i + 2, idx + i + 1,
                            (size_t)(*n - i - 1) * sizeof *idx);
                    idx[i + 1] = (u16)(DP_XCOL | (u16)k);
                    (*n)++;
                    break;
                }
    return rc;
}

int proto_page_args(proto_call *c, int t, const char *keys, u16 *page,
                    u16 *idx, u16 *n)
{
    int rc;

    rc = proto_only_keys(c, keys);
    if (rc == ENG_OK)
        rc = proto_page_size(c, page);
    if (rc == ENG_OK)
        rc = field_list(c, t, proto_get(c->r, "FIELDS"), idx, n);
    return rc;
}

/* ---------------------------------------------------------------------
 * Replies.
 * --------------------------------------------------------------------- */
int proto_done(const proto_call *c, int rc)
{
    if (rc == ENG_OK)
        proto_ok(c->o, NULL);
    return rc;
}

int proto_ok_num(const proto_call *c, const char *key, u32 n)
{
    proto_line_begin(c->o, "+OK");
    proto_line_num(c->o, key, n);
    proto_line_end(c->o);
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * Record, list and memo output.
 * --------------------------------------------------------------------- */

/* End a +DATA block after a failure with proto_err: "K ERROR=code" and
   the terminator.  A line still being built is dropped, as starting the
   K line discards it; this is how a record line with a value left out
   (the writer's cut flag, which the limits keep from happening) is never
   sent short.  Returns rc. */
static int data_error(proto_out *o, int rc)
{
    proto_err(o, rc, NULL, NULL);
    return rc;
}

void proto_rows_init(proto_rows *r, int t, const u16 *idx, u16 n,
                     proto_next_fn next, proto_more_fn more,
                     proto_curs_fn cursor, void *arg)
{
    r->t = t;
    r->idx = idx;
    r->n = n;
    r->lead = NULL;
    r->leadval[0] = '\0';
    r->xt = t;
    r->xidx = NULL;
    r->xn = 0;
    r->xok = 0;
    r->next = next;
    r->more = more;
    r->cursor = cursor;
    r->arg = arg;
    r->xk = DP_NXCOL;
}

/* The name of column f, a field or a computed column, of table t. */
static const char *col_name(eng *e, int t, u16 f)
{
    if (f & DP_XCOL)
        return dp_xcols[f ^ DP_XCOL].name;
    return eng_field(e, t, f)->name;
}

void proto_send_fline(const proto_call *c, const proto_rows *r)
{
    eng *e;
    proto_out *o;
    u16 i;

    e = c->h->e;
    o = c->o;
    proto_line_begin(o, "F");
    if (r->lead != NULL)
        proto_line_word(o, r->lead);
    for (i = 0; i < r->n; i++)
        proto_line_word(o, col_name(e, r->t, r->idx[i]));
    for (i = 0; i < r->xn; i++)
        proto_line_word(o, eng_field(e, r->xt, r->xidx[i])->name);
    proto_line_end(o);
}

void proto_send_cursor(proto_out *o, const char *cursor)
{
    if (cursor[0] == '\0')
        return;
    proto_line_begin(o, "C");
    proto_line_word(o, cursor);
    proto_line_end(o);
}

int proto_send_rows(proto_call *c, proto_rows *r, u16 page)
{
    eng *e;
    proto_out *o;
    char val[ENG_MAXVAL];
    char cur[ENG_CURSORMAX];
    u32 recno;
    u16 i;
    u16 f;
    u16 sent;
    int rc;
    int more;

    e = c->h->e;
    o = c->o;
    sent = 0;
    proto_data_begin(o);
    proto_send_fline(c, r);

    /* The first entry is always examined, so every request moves the
       cursor forward; the room is checked before each later one. */
    while (sent < page && !proto_out_failed(o)) {
        if (sent > 0 && proto_out_room(o) < PROTO_ROOMMIN)
            break;
        rc = r->next(c, r, &recno);
        if (rc != ENG_OK)
            return data_error(o, rc);
        if (recno == 0)
            break;
        proto_line_begin(o, "R");
        if (r->lead != NULL)
            proto_line_value(o, r->leadval);
        for (i = 0; i < r->n; i++) {
            f = r->idx[i];
            rc = f & DP_XCOL ? dp_xcols[f ^ DP_XCOL].text(c, r, val) :
                 eng_rec_text(e, r->t, f, val);
            if (rc != ENG_OK)
                return data_error(o, rc);
            proto_line_value(o, val);
        }
        for (i = 0; i < r->xn; i++) {
            val[0] = '\0';
            if (r->xok) {
                rc = eng_rec_text(e, r->xt, r->xidx[i], val);
                if (rc != ENG_OK)
                    return data_error(o, rc);
            }
            proto_line_value(o, val);
        }
        if (o->cut)
            return data_error(o, ENG_EINTERNAL);
        proto_line_end(o);
        sent++;
    }

    rc = r->more(c, r, &more);
    if (rc != ENG_OK)
        return data_error(o, rc);
    cur[0] = '\0';
    if (more)
        r->cursor(r, cur);
    proto_send_cursor(o, cur);
    proto_data_end(o);
    return ENG_OK;
}

/* The source of proto_send_page: the entries of the eng_scan in arg. */
static int scan_next(proto_call *c, proto_rows *r, u32 *recno)
{
    return eng_scan_next(c->h->e, (eng_scan *)r->arg, recno);
}

static int scan_more(proto_call *c, proto_rows *r, int *more)
{
    return eng_scan_more(c->h->e, (eng_scan *)r->arg, more);
}

static void scan_cursor(proto_rows *r, char *out)
{
    eng_scan_cursor((const eng_scan *)r->arg, out);
}

int proto_send_page(proto_call *c, eng_scan *s, int t, const u16 *idx,
                    u16 n, u16 page)
{
    proto_rows r;

    proto_rows_init(&r, t, idx, n, scan_next, scan_more, scan_cursor, s);
    return proto_send_rows(c, &r, page);
}

int proto_send_record(proto_call *c, int t)
{
    int rc;

    proto_data_begin(c->o);
    rc = proto_send_fields(c, t);
    if (rc == ENG_OK)
        proto_data_end(c->o);
    return rc;
}

int proto_send_fields(proto_call *c, int t)
{
    eng *e;
    proto_out *o;
    const eng_tdef *d;
    const dbf_field *fd;
    char val[ENG_MAXVAL];
    u16 i;
    int rc;

    e = c->h->e;
    o = c->o;
    d = eng_table_def(t);
    for (i = 0; i < d->nfields; i++) {
        fd = eng_field(e, t, i);
        if ((eng_field_flags(t, i) & ENG_F_HIDDEN) || fd->type == 'M')
            continue;
        rc = eng_rec_text(e, t, i, val);
        if (rc != ENG_OK)
            return data_error(o, rc);
        proto_line_begin(o, "K");
        proto_line_kv(o, fd->name, val);
        if (o->cut)
            return data_error(o, ENG_EINTERNAL);
        proto_line_end(o);
    }
    return ENG_OK;
}

/* Bytes of the fixed parts of a memo response: "+DATA" and "." with
   their CR LF, and the "K TRUNCATED=1" line. */
#define MEMO_FRAME      (7 + 3)
#define MEMO_TRUNCLINE  ((u32)sizeof "K TRUNCATED=1\r\n" - 1)

/* A memo of n bytes takes at most MEMO_BYTEMAX * n bytes of L lines:
   each byte is written as at most 4 (a "\xHH" escape), and each line,
   which takes at least one byte of the memo, adds 4 for its "L " and
   CR LF.  The bound is loose: every line but the last costs at most 4
   plus 16 / PROTO_MEMOLINE per byte, and only a last line of one
   escaped byte costs 8, so the first pass of proto_send_memo also runs
   for some memos that fit. */
#define MEMO_BYTEMAX    8

/* The next memo line from buf[*pos .. len-1] as written text into out
   (PROTO_MEMOLINE + 1 bytes), NUL-terminated; *pos moves past it and its
   line end.  Returns 0 when no line is left. */
static int memo_line(const u8 *buf, u16 len, u16 *pos, char *out)
{
    u16 p;
    u16 w;
    u16 plen;
    char piece[4];
    u8 b;

    p = *pos;
    w = 0;
    if (p >= len)
        return 0;
    while (p < len) {
        b = buf[p];
        if (b == 0x0D) {
            p++;
            if (p < len && buf[p] == 0x0A)
                p++;
            break;
        }
        if (b == 0x0A) {
            p++;
            break;
        }
        if (b == 0x8D && p + 1 < len && buf[p + 1] == 0x0A) {
            p += 2;
            break;
        }
        if (b == '\\') {
            piece[0] = '\\';
            piece[1] = '\\';
            plen = 2;
        } else if ((b < 0x20 && b != 0x09) || b == 0x7F) {
            piece[0] = '\\';
            piece[1] = 'x';
            piece[2] = txt_hexdig[b >> 4];
            piece[3] = txt_hexdig[b & 0x0F];
            plen = 4;
        } else {
            piece[0] = (char)b;
            plen = 1;
        }
        if (w + plen > PROTO_MEMOLINE)
            break;
        memcpy(out + w, piece, plen);
        w = (u16)(w + plen);
        p++;
    }
    out[w] = '\0';
    *pos = p;
    return 1;
}

/* Bytes of the L line holding text, CR LF included. */
static u16 memo_size(const char *text)
{
    u16 n;

    n = (u16)strlen(text);
    return (u16)(n != 0 ? 1 + 1 + n + 2 : 1 + 2);
}

int proto_send_memo(proto_call *c, u16 len)
{
    proto_out *o;
    const u8 *buf;
    char text[PROTO_MEMOLINE + 1];
    u32 total;
    u16 pos;
    int cut;

    o = c->o;
    buf = (const u8 *)eng_memobuf(c->h->e);
    total = MEMO_FRAME;
    pos = 0;
    cut = 0;
    /* First pass, needed only when the bound does not fit: does the whole
       memo fit? */
    if (MEMO_FRAME + MEMO_BYTEMAX * (u32)len > PROTO_MAXUNPAGED) {
        while (memo_line(buf, len, &pos, text))
            total += memo_size(text);
        cut = total > PROTO_MAXUNPAGED;
    }

    proto_data_begin(o);
    pos = 0;
    total = MEMO_FRAME + (cut ? MEMO_TRUNCLINE : 0);
    while (memo_line(buf, len, &pos, text)) {
        total += memo_size(text);
        if (total > PROTO_MAXUNPAGED)
            break;
        proto_line_begin(o, "L");
        if (text[0] != '\0')
            proto_line_word(o, text);
        proto_line_end(o);
    }
    if (cut)
        proto_k_num(o, "TRUNCATED", 1);
    proto_data_end(o);
    return ENG_OK;
}
