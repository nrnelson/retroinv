/*
 * USERTK.C - logins with a ticket (ENGUSER.H): the PWHASH a ticket is
 * made with, and the check of a ticket's tag against it.
 *
 * A module of its own, so that only the programs that serve a mailbox
 * link it; it reads the user record through what USERS.C shares
 * (ENG_USERS_SHARED).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENG_USERS_SHARED
#include "ENGUSER.H"

/* The FLD of a failed ticket login's F row. */
static const char k_ticket[] = "TICKET";

int eng_user_hash(eng *e, const char *name, char *hash)
{
    eng_urec u;
    int rc;

    rc = eng_user_load(e, name, &u);
    if (rc == ENG_OK)
        txt_copy(hash, u.hash, ENG_HASHLEN + 1);
    return rc;
}

/* The ticket is checked before the status, as the password is by
   eng_user_login.  A tag is matched only against a whole PWHASH, so a
   blank or damaged one matches no ticket. */
int eng_user_resume(eng *e, const char *name, int valid, const char *tag,
                    eng_login *out)
{
    eng_urec u;
    int rc;

    memset(out, 0, sizeof *out);
    rc = eng_login_load(e, name, &u);
    if (rc == ENG_ENOTFOUND)
        return eng_login_failed(e, 0, NULL, k_ticket, ENG_EBADPW);
    if (rc != ENG_OK)
        return rc;
    if (!valid || strlen(u.hash) != ENG_HASHLEN ||
        memcmp(u.hash, tag, ENG_TAGLEN) != 0)
        return eng_login_failed(e, u.id, u.name, k_ticket, ENG_EBADPW);
    if (u.status != 'A' || u.level == 0)
        return eng_login_failed(e, u.id, u.name, k_ticket, ENG_EDISABLED);
    out->level = u.level;
    out->mustchg = u.mustchg;
    txt_copy(out->user, u.name, sizeof out->user);
    txt_copy(out->fullname, u.fullname, sizeof out->fullname);
    return ENG_OK;
}
