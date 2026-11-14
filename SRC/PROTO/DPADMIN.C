/*
 * DPADMIN.C - AUDIT LIST and the ADMIN commands CLIENTS, FLUSH,
 * SHUTDOWN, PACK, REINDEX, BACKUP and RESTORE.
 *
 * The command table gives each of them ENG_LVL_ADMIN, and PACK, REINDEX,
 * BACKUP and RESTORE need the system to themselves: the gate answers
 * them ENG_EBUSY while other sessions are open.  Their work is the
 * engine's (ENGADMIN.H).  CLIENTS and SHUTDOWN need a server: in local
 * mode, where proto_host has no peers or stop function, they answer
 * ENG_ENOTAVAIL before their arguments are looked at.  The ADMIN
 * commands take no positional argument, and no pair but the DIR= that
 * BACKUP and RESTORE need.
 *
 * AUDIT LIST takes TBL= (a table name), REC= (a record ID, which needs
 * TBL=), STATION= and USER= (the actor's station and user name),
 * FIELDS= (a field list of AUDIT; every field by default), PAGE= and
 * AFTER=, and is paged as the other lists are.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGADMIN.H"
#include "PROTO.H"
#include "DPCMD.H"

static const char k_dir[] = "DIR";
static const char k_tbl[] = "TBL";
static const char k_rec[] = "REC";
static const char k_station[] = "STATION";
static const char k_user[] = "USER";
/* The keys of AUDIT LIST; the last one is its AFTER= key. */
static const char audit_keys[] = "TBL,REC,STATION,USER,FIELDS,PAGE,AFTER";
#define K_AFTER     (audit_keys + sizeof audit_keys - sizeof "AFTER")

/* AUDIT LIST [TBL=][REC=][STATION=][USER=][FIELDS=][PAGE=][AFTER=]: the
   audit rows of a table or record by record, else the newest first. */
int dp_audit_list(proto_call *c)
{
    eng_scan s;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    u32 rec;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_AUDIT, audit_keys, &page, idx, &n);
    if (rc == ENG_OK)
        rc = proto_key_id(c, k_rec, &rec);
    if (rc == ENG_OK)
        rc = eng_audit_scan(c->h->e, &s, proto_get(c->r, k_tbl), rec,
                            proto_get(c->r, k_station),
                            proto_get(c->r, k_user),
                            proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    return proto_send_page(c, &s, ENG_T_AUDIT, idx, n, page);
}

/* The columns of ADMIN CLIENTS, sent as one word after "F". */
static const char k_cols[] = "SLOT TYPE STATION USER LEVEL STATE IDLE SELF";

/* Bytes of a literal; of the longest R line of ADMIN CLIENTS, CR LF
   excluded: "R", a space before each of the eight values, four u8
   numbers of at most three digits, IDLE of TXT_U32LEN, and the three
   texts with their quotes and every character written as a four-byte
   \xHH escape; and of the rest of the answer: "+DATA", the F line and
   the terminator with their CR LF. */
#define AC_LIT(s)       (sizeof s - 1)
#define AC_TEXT(n)      (2 + 4 * (n))
#define AC_RLINE        (1 + 8 + 4 * 3 + TXT_U32LEN + \
                         AC_TEXT(PROTO_KINDLEN) + AC_TEXT(ENG_STALEN) + \
                         AC_TEXT(ENG_USERLEN))
#define AC_FRAME        (AC_LIT("+DATA\r\n") + AC_LIT("F \r\n") + \
                         AC_LIT(k_cols) + AC_LIT(".\r\n"))

/* The answer is not paged: the frame and PROTO_MAXPEERS R lines with
   their CR LF fit PROTO_MAXUNPAGED, and an R line fits PROTO_MAXRESP,
   so no value is ever left out. */
extern char fits_clients_row[1];
extern char fits_clients_row[(AC_RLINE <= PROTO_MAXRESP) ? 1 : 2];
extern char fits_clients[1];
extern char fits_clients[(AC_FRAME +
                          PROTO_MAXPEERS * (AC_RLINE + AC_LIT("\r\n")) <=
                          PROTO_MAXUNPAGED) ? 1 : 2];

/* ADMIN CLIENTS: an R line per open session, in the server's slot
   order, at most PROTO_MAXPEERS. */
int dp_admin_clients(proto_call *c)
{
    const proto_host *h;
    proto_out *o;
    proto_peer p;
    u16 i;
    int rc;

    h = c->h;
    o = c->o;
    if (h->peers == NULL)
        return ENG_ENOTAVAIL;
    rc = proto_args_end(c, 0, "");
    if (rc != ENG_OK)
        return rc;
    proto_data_begin(o);
    proto_line_begin(o, "F");
    proto_line_word(o, k_cols);
    proto_line_end(o);
    for (i = 0; i < PROTO_MAXPEERS && h->peers(h->hostarg, i, &p) == 0;
         i++) {
        proto_line_begin(o, "R");
        proto_line_unum(o, p.slot);
        proto_line_value(o, p.kind);
        proto_line_value(o, p.station);
        proto_line_value(o, p.user);
        proto_line_unum(o, p.level);
        proto_line_unum(o, p.state);
        proto_line_unum(o, p.idle);
        proto_line_unum(o, p.self);
        proto_line_end(o);
    }
    proto_data_end(o);
    return ENG_OK;
}

/* ADMIN FLUSH: every file with unwritten changes (eng_flush). */
int dp_admin_flush(proto_call *c)
{
    int rc;

    rc = proto_args_end(c, 0, "");
    if (rc == ENG_OK)
        rc = eng_flush(c->h->e);
    return proto_done(c, rc);
}

/* ADMIN SHUTDOWN: +OK, then the server begins its orderly shutdown. */
int dp_admin_shutdown(proto_call *c)
{
    int rc;

    if (c->h->stop == NULL)
        return ENG_ENOTAVAIL;
    rc = proto_args_end(c, 0, "");
    if (rc != ENG_OK)
        return rc;
    proto_ok(c->o, NULL);
    c->h->stop(c->h->hostarg);
    return ENG_OK;
}

/* ADMIN PACK, REINDEX, BACKUP DIR= and RESTORE DIR=: the engine's op
   (ENGADMIN.H); "+OK REMOVED=n" for PACK, "+OK" for the others. */
static int file_cmd(proto_call *c, int op)
{
    const char *dir;
    u32 removed;
    int rc;

    dir = NULL;
    rc = proto_args_end(c, 0, op >= ENG_ADM_BACKUP ? k_dir : "");
    if (rc == ENG_OK && op >= ENG_ADM_BACKUP)
        rc = proto_need_key(c, k_dir, &dir);
    if (rc == ENG_OK)
        rc = eng_admin(c->h->e, op, dir, &removed);
    if (rc == ENG_OK && op == ENG_ADM_PACK)
        return proto_ok_num(c, "REMOVED", removed);
    return proto_done(c, rc);
}

int dp_admin_pack(proto_call *c)
{
    return file_cmd(c, ENG_ADM_PACK);
}

int dp_admin_reindex(proto_call *c)
{
    return file_cmd(c, ENG_ADM_REINDEX);
}

int dp_admin_backup(proto_call *c)
{
    return file_cmd(c, ENG_ADM_BACKUP);
}

int dp_admin_restore(proto_call *c)
{
    return file_cmd(c, ENG_ADM_RESTORE);
}
