/*
 * RQERR.C - the client's texts and next steps for failed requests and
 * for operations above the user's level (RQERR.H).
 *
 * One switch over the code (code_msg) picks the message, how it is shown
 * and the action, so each case is only data; rqe_class then writes the
 * text, the code as ^1.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "RQ.H"
#include "MSG.H"
#define RQERR_CLASS
#include "RQERR.H"

/* rqe_text finds MT_EIO..MT_EINUSE by RQ_EIO..RQ_EINUSE (the build
   fails otherwise). */
extern char rqe_errorder[1];
extern char rqe_errorder[(MT_EINUSE - MT_EIO == RQ_EINUSE - RQ_EIO) ? 1 : 2];

/* MT_PERM naming level, a msg_sub result. */
static const char *perm_text(u8 level)
{
    char lv[TXT_U32LEN + 1];

    txt_u32(lv, (u32)level);
    return msg_sub(MT_PERM, lv, NULL);
}

/* The message of code c, with e->show and e->act set for it (e->act
   left RQE_STAY where the code has no action); 0, with RQE_LINE, for
   102 and 202, whose text is the server's.  Kept apart from rqe_class
   so that neither is too large for Watcom C 6.5 to optimise. */
static u16 code_msg(rqe *e, int c)
{
    u16 m;

    e->show = RQE_BOX;
    switch (c) {
    case RQ_ELOST:
        e->act = RQE_LOST;
        return MT_LOST;
    case RQ_ECLOSED:
    case RQ_ESHUTDOWN:
        e->act = RQE_QUIT;
        return MT_ENDED;
    case RQ_EIO:
    case RQ_EINDEX:
    case RQ_EFULL:
    case RQ_EHANDLES:
    case RQ_EINUSE:
        return MT_DBERR;
    case RQ_ENOLOGIN:
        e->act = RQE_LOGIN;
        return MT_EXPIRED;
    case RQ_EMUSTCHG:
        e->act = RQE_LOGIN;
        return MT_MUSTCHG;
    case RQ_ENOTAVAIL:
        m = MT_NOTAVAIL;
        break;
    case RQ_ENOTFOUND:
        m = MT_NOTFOUND;
        break;
    case RQ_EDUP:
        m = MT_DUP;
        break;
    case RQ_ECURSOR:
        m = MT_RESTART;
        break;
    case RQ_EBADPW:
        m = MT_BADPW;
        break;
    case RQ_EDISABLED:
        m = MT_DISABLED;
        break;
    case RQ_EBUSY:
        m = MT_BUSY;
        break;
    case RQ_EPERM:
        m = MT_PERM;
        break;
    case RQ_EARG:
    case RQ_ECONSTR:
        m = 0;
        break;
    default:
        return MT_INTERNAL;
    }
    e->show = RQE_LINE;
    return m;
}

void rqe_class(rqe *e, const rq_res *res, u8 level)
{
    char num[TXT_U32LEN + 1];
    const char *t;
    u16 m;
    u16 n;
    int c;

    c = res->code;
    e->act = RQE_STAY;
    if (c == RQ_OK) {
        e->show = RQE_NONE;
        e->text[0] = '\0';
        return;
    }
    txt_u32(num, (u32)(c < 0 ? 0 : c));
    if (res->closed && c != RQ_ELOST)
        c = RQ_ECLOSED;
    m = code_msg(e, c);
    if (m == MT_PERM)
        t = perm_text(level);
    else if (m == 0)
        t = res->text;
    else
        t = msg_sub(m, num, m == MT_DBERR ? rqe_text(c) : NULL);
    txt_copy(e->text, t, (u16)sizeof e->text);
    if (m == 0)
        e->text[0] = (char)TXT_UPPER(e->text[0]);
    if (c == RQ_EINDEX) {
        n = (u16)strlen(e->text);
        e->text[n++] = '\n';
        msg_get(MT_REINDEX, e->text + n, (u16)(sizeof e->text - n));
    }
}

const char *rqe_text(int code)
{
    char num[TXT_U32LEN + 1];

    if (code >= RQ_EIO && code <= RQ_EINUSE)
        return msg((u16)(MT_EIO + (code - RQ_EIO)));
    txt_u32(num, (u32)(code < 0 ? 0 : code));
    return msg_sub(MT_EOTHER, num, NULL);
}

const char *rqe_doserr(int code)
{
    /* The DOS errors with a text, and their texts. */
    static const u8 codes[] = {
        CF_ENOENT, CF_ENOPATH, CF_EACCES, CF_EMFILE, CF_ESHARE, CF_ELOCK,
        CF_EFULL, CF_EINVAL
    };
    static const u8 texts[sizeof codes] = {
        MT_CFNOPATH, MT_CFNOPATH, MT_CFACCESS, MT_EHANDLES, MT_EINUSE,
        MT_EINUSE, MT_EFULL, MT_CFNAME
    };
    char num[TXT_U32LEN + 1];
    u16 i;

    for (i = 0; i < sizeof codes; i++)
        if (code == codes[i])
            return msg(texts[i]);
    txt_u32(num, (u32)(u16)code);
    return msg_sub(MT_CFOTHER, num, NULL);
}

const char *rqe_deny(u8 level, u8 need)
{
    return level >= need ? NULL : perm_text(level);
}
