/*
 * TMBOX.C - the server's mailbox transport (TMBOX.H).
 *
 * The transport runs at most one session, on one CONN slot, with two
 * files open: the request and the response being written, the .TMP.
 * The session opens by reading the whole request into a far block, the
 * transport's own of CONN_RXSZ bytes when it fits there with the CR LF
 * that may be added, else one of its size taken for the session; CONN
 * is then handed the block a piece of up to CONN_RXSZ bytes at a time.
 * The callbacks of conn_tops only mark work due on the slot, as MB_
 * bits; a pass of the running session first writes the output that
 * waits, then hands over the next piece once CONN has given the last
 * back, then ends the session when it is to end.
 *
 * The request is overwritten with zeros in the file and committed as
 * soon as it is read, before CONN is given any of it.  Every DOS buffer
 * that held any part of it, one the writer of the file filled included,
 * then holds zeros before anything else is written, so no later write
 * can carry the request, a password in it included, to the disk: the
 * first write into a sector past a file's end takes a DOS buffer as it
 * is, and the rest of that sector goes to the disk with what the buffer
 * last held.  For the same reason the response, which may hold a login
 * ticket, ends in a sector of its own: once its last output is written
 * the rest of the .TMP's last sector is written with zeros and the file
 * cut back to the response, so that no DOS buffer carries an earlier
 * session's ticket or request into it.  The response itself cannot be
 * wiped here, as its client must read it; TMBOX.H says what that
 * leaves.  The block is zeroed at the session's end, a block of the
 * session's own then given back, after CONN has let go of it; the
 * transport's block thus holds no request between sessions, and
 * tmbox_stop frees it as it is.
 *
 * A look, while no session runs, first tries again to put in place
 * the .TMP files left in RESPONSE\, then picks the next request.  While
 * a .TMP waits there, a request of its station is passed over: it may
 * be the one the .TMP answers, which could not be removed, and its
 * session would write the same .TMP.  A failure a look meets is
 * reported once: the station goes into a short list of MB_QUIET
 * entries, which keeps it while each look meets its file and until a
 * session of it ends; when the list is full a failure is reported each
 * time.
 */
#define COMPAT_FIND
#define COMPAT_DISK
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TMBOX.H"

/* A path in the mailbox: the dir, RESPONSE\ (the longer directory) and
   an 8.3 name with its NUL. */
#define MB_PATH         (ENG_MAXDIR + sizeof TMBOX_RSPDIR + DOS_FINDNAME)
#define MB_QUIET        4       /* stations not reported again          */
#define MB_CRLF         (sizeof k_crlf - 1) /* the CR LF that may be added */

/* The kinds of mailbox file: a request, a response being written and a
   response, in the order of the extensions in mb_path. */
#define MB_REQ          0
#define MB_TMP          1
#define MB_RSP          2

/* Bits of a slot's work due. */
#define MB_RX           1       /* CONN has given the piece back        */
#define MB_TX           2       /* output waits                         */
#define MB_END          4       /* the session is to end                */

typedef struct {
    const char *dir;            /* the mailbox, NULL when not started   */
    cmem_blk *rx;               /* CONN_RXSZ bytes for a short request  */
    cmem_blk *blk;              /* the session's request, rx or its own;
                                   NULL between sessions                */
    tmbox_fail_fn watch;
    u32  looked;                /* tick of the last look                */
    int  req;                   /* the request's handle                 */
    int  tmp;                   /* the .TMP's handle                    */
    u16  at;                    /* the next piece's offset in blk       */
    u16  len;                   /* bytes of blk to hand over            */
    u8   conn;                  /* the session's slot                   */
    u8   live;                  /* 1 while a session runs               */
    u8   eager;                 /* 1: look on the next pass             */
    u8   held;                  /* 1: a .TMP waits for its rename       */
    u8   clean;                 /* the request is all zeros in the file */
    u8   broke;                 /* a write of the .TMP failed           */
    u8   seen;                  /* bit i: this look met quiet[i]        */
    char sta[TMBOX_STALEN + 1]; /* the session's station                */
    char last[TMBOX_STALEN + 1];    /* the station served last, ""      */
    char quiet[MB_QUIET][TMBOX_STALEN + 1]; /* stations reported, ""    */
} mailbox;

static mailbox tmbox_mb;
static u8 tmbox_due[CONN_MAX];  /* MB_ bits due, by CONN slot           */
static const char k_crlf[] = "\r\n";
static const char k_any[] = "*";        /* the name of every file       */

static void mb_kick(u8 c)
{
    tmbox_due[c] |= MB_TX;
}

static void mb_rx_done(u8 c)
{
    tmbox_due[c] |= MB_RX;
}

static void mb_close(u8 c)
{
    tmbox_due[c] |= MB_END;
}

static const conn_tops tmbox_ops = {
    mb_kick, mb_rx_done, mb_close, "MBOX", 1
};

/* Report err, unless 0, of a file of station sta on slot (from 1, 0 for
   none). */
static void fail(u8 slot, const char *sta, int err)
{
    if (err != 0 && tmbox_mb.watch != NULL)
        tmbox_mb.watch(slot, sta, err);
}

/* The entry of station sta in the quiet list ("" for a free one), or
   -1. */
static int quiet_at(const char *sta)
{
    int i;

    for (i = 0; i < MB_QUIET; i++)
        if (strcmp(tmbox_mb.quiet[i], sta) == 0)
            return i;
    return -1;
}

/* A look met station sta's file: its entry, if any, stays. */
static void met(const char *sta)
{
    int i;

    i = quiet_at(sta);
    if (i >= 0)
        tmbox_mb.seen |= (u8)(1 << i);
}

/* Report err, unless 0, of station sta on slot 0, unless sta is in the
   quiet list, which it joins when there is room. */
static void fail_once(const char *sta, int err)
{
    int i;

    if (err == 0 || quiet_at(sta) >= 0)
        return;
    i = quiet_at("");
    if (i >= 0) {
        strcpy(tmbox_mb.quiet[i], sta);
        tmbox_mb.seen |= (u8)(1 << i);
    }
    fail(0, sta, err);
}

/* Write into out (MB_PATH bytes) the path of the MB_ kind of file of
   name in the mailbox. */
static void mb_path(char *out, int kind, const char *name)
{
    static const char exts[][sizeof TMBOX_REQEXT + 1] = {
        "." TMBOX_REQEXT, "." TMBOX_TMPEXT, "." TMBOX_RSPEXT
    };
    size_t n;

    strcpy(out, tmbox_mb.dir);
    strcat(out, kind == MB_REQ ? TMBOX_REQDIR : TMBOX_RSPDIR);
    n = strlen(out);
    out[n++] = DOS_DIRSEP;
    strcpy(out + n, name);
    strcat(out, exts[kind]);
}

/* 1 when the found name name, cut here to its base name, is a station
   the transport may serve; 0 when DOS takes it for a device, whose
   files are never touched (TMBOX.H). */
static int station(char *name)
{
    char *dot;

    dot = strchr(name, '.');
    if (dot != NULL)
        *dot = '\0';
    return !txt_dosdev(name, (u16)strlen(name));
}

/* 1 when station a is served before b: the stations after the one
   served last come first, in name order, then the others. */
static int before(const char *a, const char *b)
{
    int wa;
    int wb;

    wa = strcmp(a, tmbox_mb.last) > 0;
    wb = strcmp(b, tmbox_mb.last) > 0;
    return wa != wb ? wa : strcmp(a, b) < 0;
}

/* Remove the file at path: 0 once it is gone, else the CF_E code; a
   removal that fails counts only while the file is there (DOSBox-X can
   refuse to remove a missing file with another code than file not
   found). */
static int gone(const char *path)
{
    int rc;

    rc = cf_remove(path);
    return rc != 0 && cf_exists(path) != 0 ? rc : 0;
}

/* Put the .TMP of station sta in place as its .RSP, a .RSP left from
   before removed first; when rid, the request the .TMP answers is
   removed before that, and the .TMP is not put in place while that
   request is there.
   Returns 1 once the .TMP is gone; 0 when it is left for a later try,
   as the request cannot be removed, the old .RSP cannot be (a reader
   holds it) or the rename failed; *err is then the code of the
   request's removal or of the rename, else 0. */
static int settle(const char *sta, int rid, int *err)
{
    char path[MB_PATH];
    char rsp[MB_PATH];

    *err = 0;
    mb_path(path, MB_REQ, sta);
    if (rid)
        *err = gone(path);
    if (*err != 0)
        return 0;
    mb_path(rsp, MB_RSP, sta);
    if (gone(rsp) != 0)
        return 0;
    mb_path(path, MB_TMP, sta);
    *err = cf_rename(path, rsp);
    return *err == 0;
}

/* Overwrite the request open on h with zeros from its start to its end,
   then close it, which commits the zeros: 0 or the first CF_E code. */
static int wipe(int h)
{
    u32 size;
    int rc;
    int rc2;

    rc = cf_size(h, &size);
    if (rc == 0)
        rc = cf_seek(h, 0);
    if (rc == 0)
        rc = cf_zero(h, size);
    rc2 = cf_close(h);
    return rc != 0 ? rc : rc2;
}

/* Fill the rest of the last sector of the .TMP, written to its end,
   with zeros and cut it back to its size: 0 or the first CF_E code. */
static int seal(void)
{
    u32 size;
    u16 k;
    int rc;

    rc = cf_size(tmbox_mb.tmp, &size);
    if (rc != 0)
        return rc;
    k = (u16)((u16)size % DOS_SECTOR);
    if (k == 0)
        return 0;
    rc = cf_zero(tmbox_mb.tmp, (u32)(DOS_SECTOR - k));
    return rc == 0 ? cf_truncate(tmbox_mb.tmp, size) : rc;
}

/* Write the output that waits on slot c into the .TMP: 0 or the CF_E
   code, *len the bytes written. */
static int put_out(u8 c, u16 *len)
{
    cmem_blk *b;
    u16 off;

    *len = conn_out_peek(c, &b, &off);
    return *len == 0 ? 0 : cf_writeblk(tmbox_mb.tmp, b, off, *len);
}

/* Write the output that waits on slot c into the .TMP at time now; a
   failure ends the session. */
static void send(u8 c, u32 now)
{
    u16 len;
    int rc;

    tmbox_due[c] &= (u8)~MB_TX;
    rc = put_out(c, &len);
    if (rc != 0) {
        fail((u8)(c + 1), tmbox_mb.sta, rc);
        tmbox_mb.broke = 1;
        tmbox_due[c] |= MB_END;
    } else if (len > 0) {
        conn_out_done(c, now, len);
    }
}

/* Read the session's request whole into its block, CR LF added after a
   last byte that is not LF, then overwrite it in the file with zeros
   and commit them: 0 or the CF_E code, CF_ENOMEM when no block can hold
   it. */
static int load(void)
{
    u32 size;
    u16 got;
    char ch;
    int rc;

    rc = cf_size(tmbox_mb.req, &size);
    if (rc != 0)
        return rc;
    if (size > CMEM_MAXBLK - MB_CRLF)
        return CF_ENOMEM;
    tmbox_mb.blk = size + MB_CRLF <= CONN_RXSZ ? tmbox_mb.rx :
                   cmem_alloc((u16)(size + MB_CRLF));
    if (tmbox_mb.blk == NULL)
        return CF_ENOMEM;
    rc = cf_readblk(tmbox_mb.req, tmbox_mb.blk, 0, (u16)size, &got);
    if (rc == 0 && size > 0) {
        rc = cf_seek(tmbox_mb.req, 0);
        if (rc == 0)
            rc = cf_zero(tmbox_mb.req, size);
        if (rc == 0)
            rc = dos_commit_handle(tmbox_mb.req);
    }
    if (rc != 0)
        return rc;
    tmbox_mb.clean = 1;
    tmbox_mb.len = got;
    if (got > 0) {
        cmem_get(tmbox_mb.blk, (u16)(got - 1), &ch, 1);
        if (ch != '\n') {
            cmem_put(tmbox_mb.blk, got, k_crlf, MB_CRLF);
            tmbox_mb.len = (u16)(got + MB_CRLF);
        }
    }
    return 0;
}

/* Hand slot c the next piece of the request's block. */
static void receive(u8 c)
{
    u16 n;

    n = (u16)(tmbox_mb.len - tmbox_mb.at);
    if (n > CONN_RXSZ)
        n = CONN_RXSZ;
    tmbox_due[c] &= (u8)~MB_RX;
    conn_input(c, tmbox_mb.blk, tmbox_mb.at, n);
    tmbox_mb.at = (u16)(tmbox_mb.at + n);
}

/* End the session as TMBOX.H says, the output CONN still holds written
   first unless a write failed, and give its slot back. */
static void finish(void)
{
    u16 len;
    u8 c;
    u8 slot;
    int i;
    int err;

    c = tmbox_mb.conn;
    slot = (u8)(c + 1);
    if (!tmbox_mb.broke) {
        err = put_out(c, &len);
        if (err == 0)
            err = seal();
        fail(slot, tmbox_mb.sta, err);
    }
    fail(slot, tmbox_mb.sta, cf_close(tmbox_mb.tmp));
    fail(slot, tmbox_mb.sta,
         tmbox_mb.clean ? cf_close(tmbox_mb.req) : wipe(tmbox_mb.req));
    settle(tmbox_mb.sta, 1, &err);
    fail(slot, tmbox_mb.sta, err);
    i = quiet_at(tmbox_mb.sta);
    if (i >= 0)
        tmbox_mb.quiet[i][0] = '\0';
    strcpy(tmbox_mb.last, tmbox_mb.sta);
    tmbox_due[c] = 0;
    tmbox_mb.live = 0;
    tmbox_mb.eager = 1;
    conn_closed(c);
    if (tmbox_mb.blk != NULL) {
        cmem_set(tmbox_mb.blk, 0, 0, cmem_size(tmbox_mb.blk));
        if (tmbox_mb.blk != tmbox_mb.rx)
            cmem_free(tmbox_mb.blk);
        tmbox_mb.blk = NULL;
    }
}

/* One pass of the running session at time now; the number of steps
   taken. */
static u16 serve(u32 now)
{
    u16 n;
    u8 c;

    c = tmbox_mb.conn;
    n = 0;
    if (tmbox_due[c] & MB_TX) {
        send(c, now);
        n++;
    }
    if ((tmbox_due[c] & (MB_RX | MB_END)) == MB_RX &&
        tmbox_mb.at < tmbox_mb.len) {
        receive(c);
        n++;
    }
    if ((tmbox_due[c] & MB_END) ||
        (tmbox_mb.at == tmbox_mb.len && (tmbox_due[c] & MB_RX))) {
        finish();
        n++;
    }
    return n;
}

/* 1 when station sta's request is the one its waiting .TMP answers:
   it starts with a zero, as its end left it, or holds nothing; 0 when
   it is not there or is a new one, which starts with a line and waits
   until the .TMP is in place.  *err is the CF_E code when the request
   cannot be opened or read, so that it is not known which, else 0. */
static int spent(const char *sta, int *err)
{
    char path[MB_PATH];
    char ch;
    u16 got;
    int h;

    mb_path(path, MB_REQ, sta);
    *err = cf_open(path, CF_READ | CF_DENYNONE, &h);
    if (*err != 0) {
        if (*err == CF_ENOENT)
            *err = 0;
        return 0;
    }
    ch = '\0';
    *err = cf_read(h, &ch, 1, &got);
    cf_close(h);
    return ch == '\0';
}

/* Try again to settle the .TMP files left in RESPONSE\, noting
   whether one still waits; the number renamed. */
static u16 retry(void)
{
    dos_find f;
    char pat[MB_PATH];
    char name[DOS_FINDNAME];
    u16 n;
    u16 k;
    int rc;
    int rid;
    int err;

    tmbox_mb.held = 0;
    n = 0;
    mb_path(pat, MB_TMP, k_any);
    rc = dos_find_first(&f, pat, name);
    for (k = 0; rc == 0 && k < TMBOX_SCANMAX; k++) {
        if (station(name)) {
            met(name);
            rid = spent(name, &err);
            if (err == 0 && settle(name, rid, &err)) {
                n++;
            } else {
                tmbox_mb.held = 1;
                fail_once(name, err);
            }
        }
        rc = dos_find_next(&f, name);
    }
    return n;
}

/* 1 when the .TMP of station sta waits for its rename (or cannot be
   looked for). */
static int waiting(const char *sta)
{
    char path[MB_PATH];

    mb_path(path, MB_TMP, sta);
    return cf_exists(path) != 0;
}

/* The station of the request to serve next into best (TMBOX_STALEN + 1
   bytes): 1, or 0 when there is none. */
static int pick(char *best)
{
    dos_find f;
    char pat[MB_PATH];
    char name[DOS_FINDNAME];
    u16 k;
    int rc;

    best[0] = '\0';
    mb_path(pat, MB_REQ, k_any);
    rc = dos_find_first(&f, pat, name);
    for (k = 0; rc == 0 && k < TMBOX_SCANMAX; k++) {
        if (station(name)) {
            met(name);
            if ((best[0] == '\0' || before(name, best)) &&
                !(tmbox_mb.held && waiting(name)))
                strcpy(best, name);
        }
        rc = dos_find_next(&f, name);
    }
    return best[0] != '\0';
}

/* Open the session of station sta's request at time now and load the
   request, a failure ending the session; 1 when the files were tried. */
static int begin(const char *sta, u32 now)
{
    char path[MB_PATH];
    u8 c;
    int rc;

    if (!conn_claim(&tmbox_ops, &c))
        return 0;
    mb_path(path, MB_REQ, sta);
    rc = cf_open(path, CF_RDWR | CF_DENYW, &tmbox_mb.req);
    if (rc == 0) {
        mb_path(path, MB_TMP, sta);
        rc = cf_create(path, &tmbox_mb.tmp);
        if (rc != 0)
            cf_close(tmbox_mb.req);
    }
    if (rc != 0) {
        conn_closed(c);
        fail_once(sta, rc);
        strcpy(tmbox_mb.last, sta);
        return 1;
    }
    strcpy(tmbox_mb.sta, sta);
    tmbox_mb.conn = c;
    tmbox_mb.live = 1;
    tmbox_mb.at = 0;
    tmbox_mb.len = 0;
    tmbox_mb.clean = 0;
    tmbox_mb.broke = 0;
    tmbox_due[c] = MB_RX;
    conn_open(c, now, sta);
    rc = load();
    if (rc != 0) {
        fail((u8)(c + 1), sta, rc);
        tmbox_due[c] |= MB_END;
    }
    return 1;
}

/* End the session of every RESPONSE\*.TMP a run that stopped left, as
   TMBOX.H says, each failure reported on slot 0: CF_ENOPATH when
   RESPONSE\ does not exist, else 0. */
static int recover(void)
{
    dos_find f;
    char pat[MB_PATH];
    char path[MB_PATH];
    char name[DOS_FINDNAME];
    int h;
    int rc;
    int err;

    mb_path(pat, MB_TMP, k_any);
    rc = dos_find_first(&f, pat, name);
    if (rc == CF_ENOPATH)
        return rc;
    for (; rc == 0; rc = dos_find_next(&f, name)) {
        if (station(name)) {
            mb_path(path, MB_REQ, name);
            err = cf_open(path, CF_RDWR | CF_DENYW, &h);
            if (err == 0)
                err = wipe(h);
            if (err != CF_ENOENT)
                fail(0, name, err);
            settle(name, 1, &err);
            fail(0, name, err);
        }
    }
    return 0;
}

int tmbox_start(const char *dir)
{
    dos_find f;
    char pat[MB_PATH];
    char name[DOS_FINDNAME];
    size_t n;

    n = strlen(dir);
    if (tmbox_mb.dir != NULL || n == 0 || n > ENG_MAXDIR ||
        (dir[n - 1] != '\\' && dir[n - 1] != '/' && dir[n - 1] != ':'))
        return TMBOX_EARG;
    tmbox_mb.rx = cmem_alloc(CONN_RXSZ);
    if (tmbox_mb.rx == NULL)
        return TMBOX_ENOMEM;
    tmbox_mb.dir = dir;
    mb_path(pat, MB_REQ, k_any);
    if (dos_find_first(&f, pat, name) == CF_ENOPATH || recover() != 0) {
        cmem_free(tmbox_mb.rx);
        tmbox_mb.dir = NULL;
        return TMBOX_ENODIR;
    }
    tmbox_mb.live = 0;
    tmbox_mb.eager = 1;
    tmbox_mb.held = 0;
    tmbox_mb.last[0] = '\0';
    memset(tmbox_mb.quiet, 0, sizeof tmbox_mb.quiet);
    return TMBOX_OK;
}

u16 tmbox_poll(u32 now)
{
    char sta[TMBOX_STALEN + 1];
    u16 n;
    int i;

    if (tmbox_mb.dir == NULL)
        return 0;
    if (tmbox_mb.live)
        return serve(now);
    if (!tmbox_mb.eager && now - tmbox_mb.looked < TMBOX_POLL)
        return 0;
    tmbox_mb.eager = 0;
    tmbox_mb.looked = now;
    tmbox_mb.seen = 0;
    n = (u16)(1 + retry());
    if (pick(sta))
        n = (u16)(n + begin(sta, now));
    for (i = 0; i < MB_QUIET; i++)
        if (!(tmbox_mb.seen & (1 << i)))
            tmbox_mb.quiet[i][0] = '\0';
    return n;
}

void tmbox_stop(void)
{
    if (tmbox_mb.dir == NULL)
        return;
    if (tmbox_mb.live)
        finish();
    cmem_free(tmbox_mb.rx);
    tmbox_mb.rx = NULL;
    tmbox_mb.dir = NULL;
}

const char *tmbox_dir(void)
{
    return tmbox_mb.dir != NULL ? tmbox_mb.dir : "";
}

void tmbox_watch(tmbox_fail_fn fn)
{
    tmbox_mb.watch = fn;
}
