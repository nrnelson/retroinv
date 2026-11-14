/*
 * DPUSER.C - the USER commands: LIST GET ADD SET RESET DEL.
 *
 * The command table gives every one of them ENG_LVL_ADMIN, so the
 * handlers only map the request onto the USERS.C calls.  The user name
 * is the first positional argument; its form (1..ENG_USERLEN characters
 * A-Z 0-9 '_') is checked by the engine, which upper-cases it.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGUSER.H"

static const char k_password[] = "PASSWORD";

/* The user name and the check that nothing else is positional; keys is
   the comma list of the pairs the command accepts. */
static int user_name(proto_call *c, const char *keys, const char **name)
{
    int rc;

    rc = proto_need(c, 0, "USERNAME", name);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    return rc;
}

/* USER LIST [PAGE=][AFTER=]: the default USERS fields in name order. */
int dp_user_list(proto_call *c)
{
    eng_scan s;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_USERS, "PAGE,AFTER", &page, idx, &n);
    if (rc == ENG_OK)
        rc = eng_user_scan(c->h->e, &s, proto_get(c->r, "AFTER"));
    if (rc != ENG_OK)
        return rc;
    return proto_send_page(c, &s, ENG_T_USERS, idx, n, page);
}

/* USER GET name: the record as K lines (SALT and PWHASH are hidden). */
int dp_user_get(proto_call *c)
{
    const char *name;
    u32 recno;
    int rc;

    rc = user_name(c, "", &name);
    if (rc == ENG_OK)
        rc = eng_user_get(c->h->e, name, &recno);
    if (rc != ENG_OK)
        return rc;
    return proto_send_record(c, ENG_T_USERS);
}

/* USER ADD name LEVEL= PASSWORD= [FULLNAME=][GROUP= or GROUPNAME=]. */
int dp_user_add(proto_call *c)
{
    const char *name;
    int rc;

    rc = user_name(c, "LEVEL,PASSWORD,FULLNAME,GROUP,GROUPNAME", &name);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_user_add(c->h->e, name, c->r->pair,
                                      c->r->npairs));
}

/* USER SET name [LEVEL=][FULLNAME=][GROUP= or GROUPNAME=][STATUS=]. */
int dp_user_set(proto_call *c)
{
    const char *name;
    int rc;

    rc = user_name(c, "LEVEL,FULLNAME,GROUP,GROUPNAME,STATUS", &name);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_user_set(c->h->e, name, c->r->pair,
                                      c->r->npairs));
}

/* USER RESET name PASSWORD=. */
int dp_user_reset(proto_call *c)
{
    const char *name;
    const char *pw;
    int rc;

    rc = user_name(c, k_password, &name);
    if (rc == ENG_OK)
        rc = proto_need_key(c, k_password, &pw);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_user_reset(c->h->e, name, pw));
}

/* USER DEL name. */
int dp_user_del(proto_call *c)
{
    const char *name;
    int rc;

    rc = user_name(c, "", &name);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_user_del(c->h->e, name));
}
