/*
 * T_RQERR.C - what the client tells the user about a failed request
 * (SRC/CLIENT/RQERR.C): rqe_class's show, act and text for every row of
 * its table, rqe_text, rqe_doserr and rqe_deny.
 *
 * usage: T_RQERR dir [msgdir]
 *
 * dir is unused (the runners pass a scratch directory); msgdir is the
 * directory prefix of the built INV.MSG (default: the program's own
 * directory).  The expected texts are fetched from the loaded messages by
 * their MT_ constants, never typed here.
 *
 * Covered:
 * - RQ_OK (also with the session closed);
 * - a lost link;
 * - a closed session (RQ_ECLOSED, 501, and res->closed on any code);
 * - the codes shown as one line (104, 200, 201, 204, the server's reason
 *   for 102 and 202 with its first letter in upper case, 203 naming the
 *   level);
 * - the storage errors 300..304 (301 with the reindex line);
 * - the login codes 400..404, an unknown code;
 * - the order in which the rows apply;
 * - rqe_text for each storage code, another code and a negative one;
 * - rqe_doserr for each DOS error with a text of its own and two
 *   others;
 * - rqe_deny at the boundary levels;
 * - that no text outgrows RQE_TEXTLEN, with the longest text the server
 *   can send.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "RQ.H"
#define RQERR_CLASS
#include "RQERR.H"
#include "TESTFW.H"

#define PATHMAX         80
#define SCRATCH         (2 * MSG_MAXLEN + 2)

static char g_want[SCRATCH];    /* the text a check expects             */
static char g_a[SCRATCH];       /* copies of msg results                */
static char g_b[SCRATCH];

/* Classify code (closed set when cl) with server text txt for a user of
   level into e. */
static void classify(rqe *e, int code, u8 cl, const char *txt, u8 level)
{
    rq_res r;

    memset(&r, 0, sizeof r);
    r.code = code;
    r.closed = cl;
    strcpy(r.text, txt);
    memset(e, 0x55, sizeof *e);
    rqe_class(e, &r, level);
}

/* e holds show, act and the text msg(n) alone. */
static void is_msg(const rqe *e, u8 show, u8 act, u16 n)
{
    TF_EQ(e->show, show);
    TF_EQ(e->act, act);
    TF_STREQ(e->text, msg(n));
}

/* e holds show, act and the text msg_sub(n, a1, a2). */
static void is_sub(const rqe *e, u8 show, u8 act, u16 n, const char *a1,
                   const char *a2)
{
    TF_EQ(e->show, show);
    TF_EQ(e->act, act);
    TF_STREQ(e->text, msg_sub(n, a1, a2));
}

static void test_ok(void)
{
    rqe e;

    classify(&e, RQ_OK, 0, "", 0);
    TF_EQ(e.show, RQE_NONE);
    TF_EQ(e.act, RQE_STAY);
    TF_STREQ(e.text, "");
    classify(&e, RQ_OK, 0, "some +OK text", 3);
    TF_EQ(e.show, RQE_NONE);
    TF_EQ(e.act, RQE_STAY);
    TF_STREQ(e.text, "");
    /* RQ_OK comes first: the +OK to BYE sets closed, and is no error. */
    classify(&e, RQ_OK, 1, "", 3);
    TF_EQ(e.show, RQE_NONE);
    TF_EQ(e.act, RQE_STAY);
    TF_STREQ(e.text, "");
}

static void test_closed(void)
{
    rqe e;

    classify(&e, RQ_ELOST, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_LOST, MT_LOST);
    classify(&e, RQ_ELOST, 0, "", 2);
    is_msg(&e, RQE_BOX, RQE_LOST, MT_LOST);
    classify(&e, RQ_ECLOSED, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_ECLOSED, 0, "", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_ESHUTDOWN, 0, "server shutting down", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    /* Any other code with closed set ends the session as well,
       whatever row the code would otherwise take. */
    classify(&e, RQ_ENOTFOUND, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_EPERM, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_EARG, 1, "bad", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_EIO, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_EMUSTCHG, 1, "", 0);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    classify(&e, RQ_ESTOP, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_QUIT, MT_ENDED);
    /* A lost link wins over closed. */
    classify(&e, RQ_ELOST, 1, "", 2);
    is_msg(&e, RQE_BOX, RQE_LOST, MT_LOST);
}

static void test_lines(void)
{
    rqe e;

    classify(&e, RQ_ENOTAVAIL, 0, "ignored", 2);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_NOTAVAIL);
    classify(&e, RQ_ENOTFOUND, 0, "ignored", 2);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_NOTFOUND);
    classify(&e, RQ_EDUP, 0, "ignored", 2);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_DUP);
    classify(&e, RQ_ECURSOR, 0, "ignored", 2);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_RESTART);
}

static void test_server_text(void)
{
    rqe e;

    classify(&e, RQ_EARG, 0, "NAME is required", 2);
    TF_EQ(e.show, RQE_LINE);
    TF_EQ(e.act, RQE_STAY);
    TF_STREQ(e.text, "NAME is required");
    classify(&e, RQ_ECONSTR, 0, "ID already used", 1);
    TF_EQ(e.show, RQE_LINE);
    TF_EQ(e.act, RQE_STAY);
    TF_STREQ(e.text, "ID already used");
    classify(&e, RQ_ECONSTR, 0, "", 1);
    TF_EQ(e.show, RQE_LINE);
    TF_EQ(e.act, RQE_STAY);
    TF_STREQ(e.text, "");
    classify(&e, RQ_ECONSTR, 0, "item is on loan", 1);
    TF_STREQ(e.text, "Item is on loan");
    classify(&e, RQ_EARG, 0, "bad date", 2);
    TF_STREQ(e.text, "Bad date");
    classify(&e, RQ_EARG, 0, "1 too many", 2);
    TF_STREQ(e.text, "1 too many");
}

static void test_perm(void)
{
    rqe e;
    u8 lv;

    for (lv = 0; lv <= 5; lv++) {
        classify(&e, RQ_EPERM, 0, "ignored", lv);
        TF_EQ(e.show, RQE_LINE);
        TF_EQ(e.act, RQE_STAY);
        TF_CHECK(strstr(e.text, "ignored") == NULL);
        /* The same text rqe_deny gives, naming the level. */
        strcpy(g_a, e.text);
        strcpy(g_b, rqe_deny(lv, (u8)(lv + 1)));
        TF_STREQ(g_a, g_b);
    }
    classify(&e, RQ_EPERM, 0, "", 2);
    strcpy(g_a, e.text);
    classify(&e, RQ_EPERM, 0, "", 3);
    TF_CHECK(strcmp(g_a, e.text) != 0);
    TF_CHECK(strchr(e.text, '3') != NULL);
    classify(&e, RQ_EPERM, 0, "", 255);
    TF_CHECK(strstr(e.text, "255") != NULL);
}

/* The storage code c: a box MT_DBERR with the code and rqe_text(c), and
   the reindex line after a break for 301 only. */
static void one_dberr(int code, const char *digits, int reindex)
{
    rqe e;

    classify(&e, code, 0, "server text", 2);
    TF_EQ(e.show, RQE_BOX);
    TF_EQ(e.act, RQE_STAY);
    strcpy(g_want, msg_sub(MT_DBERR, digits, rqe_text(code)));
    if (reindex) {
        strcat(g_want, "\n");
        strcat(g_want, msg(MT_REINDEX));
    }
    TF_STREQ(e.text, g_want);
    TF_CHECK(strstr(e.text, "server text") == NULL);
    TF_CHECK(strstr(e.text, digits) != NULL);
    TF_CHECK((strchr(e.text, '\n') != NULL) == reindex);
}

static void test_dberr(void)
{
    one_dberr(RQ_EIO, "300", 0);
    one_dberr(RQ_EINDEX, "301", 1);
    one_dberr(RQ_EFULL, "302", 0);
    one_dberr(RQ_EHANDLES, "303", 0);
    one_dberr(RQ_EINUSE, "304", 0);
}

static void test_login(void)
{
    rqe e;

    classify(&e, RQ_EBADPW, 0, "ignored", 0);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_BADPW);
    classify(&e, RQ_EBUSY, 0, "ignored", 0);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_BUSY);
    classify(&e, RQ_EDISABLED, 0, "ignored", 0);
    is_msg(&e, RQE_LINE, RQE_STAY, MT_DISABLED);
    classify(&e, RQ_ENOLOGIN, 0, "ignored", 0);
    is_msg(&e, RQE_BOX, RQE_LOGIN, MT_EXPIRED);
    classify(&e, RQ_EMUSTCHG, 0, "ignored", 0);
    is_msg(&e, RQE_BOX, RQE_LOGIN, MT_MUSTCHG);
    /* The level does not matter. */
    classify(&e, RQ_ENOLOGIN, 0, "", 3);
    is_msg(&e, RQE_BOX, RQE_LOGIN, MT_EXPIRED);
}

static void test_internal(void)
{
    rqe e;

    classify(&e, RQ_EINTERNAL, 0, "ignored", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "500", NULL);
    classify(&e, 777, 0, "ignored", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "777", NULL);
    classify(&e, RQ_ESYNTAX, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "100", NULL);
    classify(&e, RQ_EUNKNOWN, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "101", NULL);
    classify(&e, RQ_ETOOLONG, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "103", NULL);
    classify(&e, RQ_ESTOP, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "2", NULL);
    classify(&e, 405, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "405", NULL);
    classify(&e, 305, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "305", NULL);
    classify(&e, 32767, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "32767", NULL);
    /* A negative code reads as 0 where the text shows it. */
    classify(&e, -7, 0, "", 2);
    is_sub(&e, RQE_BOX, RQE_STAY, MT_INTERNAL, "0", NULL);
}

static void test_text(void)
{
    static const int codes[5] = { RQ_EIO, RQ_EINDEX, RQ_EFULL, RQ_EHANDLES,
                                  RQ_EINUSE };
    static const u16 mts[5] = { MT_EIO, MT_EINDEX, MT_EFULL, MT_EHANDLES,
                                MT_EINUSE };
    u16 i;

    for (i = 0; i < 5; i++) {
        TF_STREQ(rqe_text(codes[i]), msg(mts[i]));
        TF_CHECK(rqe_text(codes[i])[0] != '\0');
    }
    TF_STREQ(rqe_text(305), msg_sub(MT_EOTHER, "305", NULL));
    TF_STREQ(rqe_text(299), msg_sub(MT_EOTHER, "299", NULL));
    TF_STREQ(rqe_text(RQ_EINTERNAL), msg_sub(MT_EOTHER, "500", NULL));
    TF_STREQ(rqe_text(0), msg_sub(MT_EOTHER, "0", NULL));
    TF_STREQ(rqe_text(-1), msg_sub(MT_EOTHER, "0", NULL));
    TF_STREQ(rqe_text(-300), msg_sub(MT_EOTHER, "0", NULL));
    TF_STREQ(rqe_text(-32767 - 1), msg_sub(MT_EOTHER, "0", NULL));
    /* The result is a msg result: valid through MSG_NBUF - 1 calls. */
    strcpy(g_a, rqe_text(RQ_EIO));
    strcpy(g_b, rqe_text(RQ_EFULL));
    TF_STREQ(g_a, msg(MT_EIO));
    TF_STREQ(g_b, msg(MT_EFULL));
}

static void test_doserr(void)
{
    TF_STREQ(rqe_doserr(CF_ENOENT), msg(MT_CFNOPATH));
    TF_STREQ(rqe_doserr(CF_ENOPATH), msg(MT_CFNOPATH));
    TF_STREQ(rqe_doserr(CF_EACCES), msg(MT_CFACCESS));
    TF_STREQ(rqe_doserr(CF_EMFILE), msg(MT_EHANDLES));
    TF_STREQ(rqe_doserr(CF_ESHARE), msg(MT_EINUSE));
    TF_STREQ(rqe_doserr(CF_ELOCK), msg(MT_EINUSE));
    TF_STREQ(rqe_doserr(CF_EFULL), msg(MT_EFULL));
    TF_STREQ(rqe_doserr(CF_EINVAL), msg(MT_CFNAME));
    TF_STREQ(rqe_doserr(CF_EBADF), msg_sub(MT_CFOTHER, "6", NULL));
    TF_STREQ(rqe_doserr(CF_ENOMEM), msg_sub(MT_CFOTHER, "8", NULL));
}

static void test_deny(void)
{
    u8 lv;
    u8 need;

    TF_CHECK(rqe_deny(0, 0) == NULL);
    TF_CHECK(rqe_deny(1, 1) == NULL);
    TF_CHECK(rqe_deny(5, 3) == NULL);
    TF_CHECK(rqe_deny(255, 0) == NULL);
    TF_CHECK(rqe_deny(255, 255) == NULL);
    for (lv = 0; lv < 6; lv++)
        for (need = 0; need < 6; need++) {
            if (lv >= need)
                TF_CHECK(rqe_deny(lv, need) == NULL);
            else
                TF_CHECK(rqe_deny(lv, need) != NULL);
        }
    TF_CHECK(rqe_deny(254, 255) != NULL);
    TF_CHECK(rqe_deny(0, 255) != NULL);
    TF_CHECK(rqe_deny(0, 1) != NULL);
    /* The text names the level, not the one needed. */
    strcpy(g_a, rqe_deny(2, 3));
    TF_STREQ(g_a, msg_sub(MT_PERM, "2", NULL));
    TF_STREQ(rqe_deny(2, 9), g_a);
    TF_STREQ(rqe_deny(0, 3), msg_sub(MT_PERM, "0", NULL));
    TF_STREQ(rqe_deny(254, 255), msg_sub(MT_PERM, "254", NULL));
}

/* No text outgrows RQE_TEXTLEN, with the longest text the server can
   send, for every code the table has and levels around the edges. */
static void test_length(void)
{
    static const int codes[] = {
        RQ_OK, RQ_ELOST, RQ_ECLOSED, RQ_ESHUTDOWN, RQ_ENOTAVAIL,
        RQ_ENOTFOUND, RQ_EDUP, RQ_ECURSOR, RQ_EARG, RQ_ECONSTR, RQ_EPERM,
        RQ_EIO, RQ_EINDEX, RQ_EFULL, RQ_EHANDLES, RQ_EINUSE, RQ_ENOLOGIN,
        RQ_EBADPW, RQ_EBUSY, RQ_EDISABLED, RQ_EMUSTCHG, RQ_EINTERNAL,
        RQ_ESYNTAX, 32767, -32767 - 1, 0
    };
    char big[RQ_TEXTLEN + 1];
    rqe e;
    u16 i;
    u8 cl;

    memset(big, 'W', RQ_TEXTLEN);
    big[RQ_TEXTLEN] = '\0';
    for (i = 0; i < sizeof codes / sizeof codes[0]; i++)
        for (cl = 0; cl < 2; cl++) {
            classify(&e, codes[i], cl, big, 255);
            TF_CHECK(strlen(e.text) <= RQE_TEXTLEN);
            TF_CHECK(e.show <= RQE_BOX);
            TF_CHECK(e.act <= RQE_LOST);
        }
    classify(&e, RQ_EARG, 0, big, 255);
    TF_STREQ(e.text, big);
    classify(&e, RQ_ECONSTR, 0, big, 0);
    TF_STREQ(e.text, big);
    TF_CHECK(strlen(rqe_deny(255 - 1, 255)) <= MSG_MAXLEN);
    TF_CHECK(strlen(rqe_text(RQ_EHANDLES)) <= MSG_MAXLEN);
}

int main(int argc, char **argv)
{
    char mdir[PATHMAX];

    tf_begin("T_RQERR");
    tf_progdir(mdir, PATHMAX, argv[0]);
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("T_RQERR: %s\n", msg_why());
        return tf_done();
    }
    test_ok();
    test_closed();
    test_lines();
    test_server_text();
    test_perm();
    test_dberr();
    test_login();
    test_internal();
    test_text();
    test_doserr();
    test_deny();
    test_length();
    msg_free();
    return tf_done();
}
