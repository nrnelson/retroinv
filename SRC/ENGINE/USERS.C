/*
 * USERS.C - the password hash, logins and user accounts (USERS.DBF).
 *
 * A user is a live USERS.DBF record found through USERS1 (USERNAME).
 * Its password is never stored, only PWHASH and the SALT it was hashed
 * with.  The salt is made anew for every password change, so the same
 * password gives a different PWHASH each time it is set; ENGUSER.H
 * gives both recipes and the rules for names and passwords.  Every
 * function here also enforces:
 *   - a login needs a live user with STATUS A and a LEVEL of 1..3;
 *   - no change may leave the system without a live, active level-3
 *     user.
 * USERS.DBF has no index on LEVEL, so the last-administrator check reads
 * every record of the table; the table holds one record per account
 * ever made until PACK, which keeps that read short, and it happens only
 * for an administrator's own level, status or deletion changing.
 * USERTK.C adds the logins with a ticket on the record reading and the
 * failed-login row this module shares (ENG_USERS_SHARED).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#define ENG_USERS_SHARED
#include "ENGUSER.H"

#define M32         ((u32)0xFFFFFFFFUL) /* keeps sums and shifts to 32 bits */

#define F_USERID    ENG_FIELD(USERS, USER_ID)
#define F_USERNAME  ENG_FIELD(USERS, USERNAME)
#define F_FULLNAME  ENG_FIELD(USERS, FULLNAME)
#define F_GROUP     ENG_FIELD(USERS, GROUPNAME)
#define F_LEVEL     ENG_FIELD(USERS, LEVEL)
#define F_SALT      ENG_FIELD(USERS, SALT)
#define F_PWHASH    ENG_FIELD(USERS, PWHASH)
#define F_STATUS    ENG_FIELD(USERS, STATUS)
#define F_MUSTCHG   ENG_FIELD(USERS, MUSTCHG)
#define F_LASTLOGIN ENG_FIELD(USERS, LASTLOGIN)
#define F_CREATED   ENG_FIELD(USERS, CREATED)

/* Protocol keys: the pairs of USER ADD and USER SET, and the
   arguments the errors of PASSWD and USER RESET name. */
static const char k_level[] = "LEVEL";
static const char k_fullname[] = "FULLNAME";
static const char k_group[] = "GROUP";
static const char k_password[] = "PASSWORD";
static const char k_status[] = "STATUS";
static const char k_new[] = "NEW";

/* The seeded user's first password, the user name in lower case, which
   also gives its name and group, as both fields are stored upper-cased
   (ENG_F_UPPER); and its salt. */
static const char k_admin[] = "admin";
static const char k_salt0[] = "0000";

/* MUSTCHG set. */
static const char k_true[] = "T";

/* Longest padded eng_mix128 message: the text, the 80 byte, up to three
   zero bytes and the length word. */
#define MIXBUF      (ENG_MAXSEED + 8)

/* ---------------------------------------------------------------------
 * The 128-bit mix.
 *
 * Every word is a u32 (unsigned long on the 16-bit compilers), so the
 * shifts never act on a 16-bit int; each result is masked to 32 bits so
 * the arithmetic stays modulo 2^32 even where u32 were wider.
 * --------------------------------------------------------------------- */

/* x rotated left by n, 0 < n < 32. */
static u32 rotl32(u32 x, int n)
{
    x &= M32;
    return ((x << n) | (x >> (32 - n))) & M32;
}

/* One round over the state s[0..3] = a b c d. */
static void mix_round(u32 *s)
{
    s[0] = (s[0] + s[1]) & M32;  s[3] = rotl32(s[3] ^ s[0], 16);
    s[2] = (s[2] + s[3]) & M32;  s[1] = rotl32(s[1] ^ s[2], 12);
    s[0] = (s[0] + s[1]) & M32;  s[3] = rotl32(s[3] ^ s[0], 8);
    s[2] = (s[2] + s[3]) & M32;  s[1] = rotl32(s[1] ^ s[2], 7);
}

void eng_mix128(const u8 *msg, u16 len, char *out)
{
    u8  buf[MIXBUF];
    u32 s[4];
    u32 t[4];
    u16 n;
    u16 i;
    int w;
    int k;

    if (len > ENG_MAXSEED)
        len = ENG_MAXSEED;
    if (len > 0)
        memcpy(buf, msg, len);
    n = len;
    buf[n++] = 0x80;
    while ((n & 3U) != 0)
        buf[n++] = 0;
    put_le32(buf + n, len);
    n = (u16)(n + 4U);

    s[0] = 0x67452301UL;
    s[1] = 0xEFCDAB89UL;
    s[2] = 0x98BADCFEUL;
    s[3] = 0x10325476UL;
    for (i = 0; i < n; i = (u16)(i + 4U)) {
        s[0] = (s[0] ^ get_le32(buf + i)) & M32;
        mix_round(s);
        mix_round(s);
    }
    for (w = 0; w < 4; w++)
        t[w] = s[w];
    for (k = 0; k < ENG_HASHROUNDS; k++)
        mix_round(s);
    for (w = 0; w < 4; w++)
        s[w] = (s[w] + t[w]) & M32;

    for (w = 0; w < 4; w++)
        for (k = 0; k < 8; k++)
            out[w * 8 + k] =
                txt_hexdig[(int)((s[w] >> (28 - 4 * k)) & (u32)0xF)];
    out[ENG_HASHLEN] = '\0';
    txt_wipe(buf, (u16)sizeof buf);
    txt_wipe(s, (u16)sizeof s);
    txt_wipe(t, (u16)sizeof t);
}

void eng_pw_hash(const char *salt, const char *pw, char *out)
{
    u8  msg[ENG_MAXSEED];
    u16 n;

    n = 0;
    while (n < ENG_SALTLEN && salt[n] != '\0') {
        msg[n] = (u8)salt[n];
        n++;
    }
    while (*pw != '\0' && n < ENG_MAXSEED)
        msg[n++] = (u8)*pw++;
    eng_mix128(msg, n, out);
    txt_wipe(msg, (u16)sizeof msg);
}

int eng_pw_valid(const char *pw)
{
    u16 n;

    for (n = 0; pw[n] != '\0'; n++) {
        if ((u8)pw[n] < 0x21 || (u8)pw[n] > 0x7E || n >= ENG_PWMAX)
            return 0;
    }
    return n >= ENG_PWMIN;
}

/* ---------------------------------------------------------------------
 * Names, numbers and fields.
 * --------------------------------------------------------------------- */

/* Upper-case name into out (ENG_USERLEN + 1 bytes); 1 when it is a
   well-formed user name, else 0. */
static int user_name(const char *name, char *out)
{
    u16 n;
    char c;

    for (n = 0; name[n] != '\0'; n++) {
        if (n >= ENG_USERLEN)
            return 0;
        c = (char)TXT_UPPER(name[n]);
        if (!(TXT_ISALPHA(c) || TXT_ISDIGIT(c) || c == '_'))
            return 0;
        out[n] = c;
    }
    out[n] = '\0';
    return n > 0;
}

/* Value of the decimal digits of s (0 when there are none). */
static u32 get_dec(const char *s)
{
    u32 v;

    v = 0;
    while (TXT_ISDIGIT(*s))
        v = v * (u32)10 + (u32)(*s++ - '0');
    return v;
}

/* Text of field f of the USERS buffer into out (ENG_MAXVAL). */
static int user_text(eng *e, u16 f, char *out)
{
    return eng_rec_text(e, ENG_T_USERS, f, out);
}

/* The level of a LEVEL text: 1..3, or 0 for anything else. */
static u8 level_of(const char *v)
{
    if (v[0] >= '1' && v[0] <= '3' && v[1] == '\0')
        return (u8)(v[0] - '0');
    return 0;
}

/* Fill u from the USERS buffer, which holds record recno: every field
   is read in turn, and those the rules use are kept. */
static int user_fields(eng *e, u32 recno, eng_urec *u)
{
    char v[ENG_MAXVAL];
    u16 f;
    int rc;

    u->recno = recno;
    for (f = 0; f < ENG_NFIELDS(USERS); f++) {
        if ((rc = user_text(e, f, v)) != ENG_OK)
            return rc;
        switch (f) {
        case F_USERID:
            u->id = get_dec(v);
            break;
        case F_LEVEL:
            u->level = level_of(v);
            break;
        case F_STATUS:
            u->status = v[0] != '\0' ? v[0] : ' ';
            break;
        case F_MUSTCHG:
            u->mustchg = v[0] == 'T';
            break;
        case F_USERNAME:
            txt_copy(u->name, v, sizeof u->name);
            break;
        case F_FULLNAME:
            txt_copy(u->fullname, v, sizeof u->fullname);
            break;
        case F_SALT:
            txt_copy(u->salt, v, sizeof u->salt);
            break;
        case F_PWHASH:
            txt_copy(u->hash, v, sizeof u->hash);
            break;
        default:
            break;
        }
    }
    return ENG_OK;
}

int eng_user_get(eng *e, const char *name, u32 *recno)
{
    char key[ENG_USERLEN + 1];
    u16 n;
    int rc;

    *recno = 0;
    if (!user_name(name, key) || strcmp(key, ENG_SYSTEM) == 0)
        return eng_fail(e, ENG_ENOTFOUND, NULL, ENG_TX_NOUSER);
    for (n = (u16)strlen(key); n < ENG_USERLEN; n++)
        key[n] = ' ';
    key[ENG_USERLEN] = '\0';
    rc = eng_seek_live(e, ENG_X_USERS1, key, recno);
    if (rc != ENG_OK)
        return rc;
    if (*recno == 0)
        return eng_fail(e, ENG_ENOTFOUND, NULL, ENG_TX_NOUSER);
    return ENG_OK;
}

int eng_user_load(eng *e, const char *name, eng_urec *u)
{
    u32 recno;
    int rc;

    rc = eng_user_get(e, name, &recno);
    if (rc != ENG_OK)
        return rc;
    return user_fields(e, recno, u);
}

int eng_login_load(eng *e, const char *name, eng_urec *u)
{
    int rc;

    rc = eng_user_load(e, name, u);
    if (rc == ENG_ENOTFOUND && eng_rec_read(e, ENG_T_USERS, 1) == ENG_OK)
        user_fields(e, 1, u);
    return rc;
}

/* The salt (ENG_SALTLEN + 1 bytes into salt) of a password change of
   user name, whose PWHASH is oldhash (NULL for a new user), made by the
   recipe of ENGUSER.H; called just before the change's eng_edit_begin. */
static int make_salt(eng *e, const char *name, const char *oldhash,
                     char *salt)
{
    u8   seed[ENG_MAXSEED];
    char text[ENG_HASHLEN + 1];
    char num[TXT_U32LEN + 1];
    u32  next;
    u16  n;
    int  rc;
    int  k;
    const char *p;

    rc = eng_seq_peek(e, ENG_T_AUDIT, &next);
    if (rc != ENG_OK)
        return rc;
    eng_stamp(e, (char *)seed);
    n = ENG_STAMPLEN;
    txt_u32(num, next);
    for (k = 0; k < 3; k++) {
        p = k == 0 ? name : k == 1 ? num : oldhash;
        if (p == NULL)
            continue;
        while (*p != '\0' && n < ENG_MAXSEED)
            seed[n++] = (u8)*p++;
    }
    eng_mix128(seed, n, text);
    memcpy(salt, text, ENG_SALTLEN);
    salt[ENG_SALTLEN] = '\0';
    return ENG_OK;
}

/* Store a new salt and the hash of pw in the open edit, with MUSTCHG
   set to mustchg ("T" or "F"). */
static int put_password(eng *e, const char *salt, const char *pw,
                        const char *mustchg)
{
    char hash[ENG_HASHLEN + 1];
    int rc;

    eng_pw_hash(salt, pw, hash);
    if ((rc = eng_edit_putf(e, F_SALT, salt)) != ENG_OK)
        return rc;
    if ((rc = eng_edit_putf(e, F_PWHASH, hash)) != ENG_OK)
        return rc;
    return eng_edit_putf(e, F_MUSTCHG, mustchg);
}

/* Store what every new user starts with in the open edit: SALT and
   PWHASH for salt and pw, MUSTCHG T, STATUS A and CREATED today. */
static int put_new(eng *e, const char *salt, const char *pw,
                   const char *today)
{
    int rc;

    if ((rc = put_password(e, salt, pw, k_true)) != ENG_OK)
        return rc;
    if ((rc = eng_edit_putf(e, F_STATUS, "A")) != ENG_OK)
        return rc;
    return eng_edit_putf(e, F_CREATED, today);
}

/* Commit the open edit of user id's new SALT and PWHASH, whose puts
   gave rc, with its one W row.  The commit audits only field changes,
   and these fields are hidden, so the row is written here: inside one
   bracket of the marker, and on the disk before the record, as a
   commit's own rows are.  A stop or a failure between the two leaves
   the marker, and the next open flags the row. */
static int commit_password(eng *e, int rc, u32 id)
{
    eng_audrow row;

    if (rc == ENG_OK)
        rc = eng_mark(e, ENG_T_USERS);
    if (rc != ENG_OK) {
        eng_edit_cancel(e);
        return rc;
    }
    row.action = ENG_A_PASSWORD;
    row.fld = eng_field_name(e, ENG_T_USERS, F_PWHASH);
    row.oldval = NULL;
    row.newval = NULL;
    rc = eng_edit_finish(e, eng_audit(e, ENG_T_USERS, id, &row), id);
    if (rc == ENG_OK)
        eng_unmark(e);
    return rc;
}

/* 1 in *found when a live, active level-3 user other than record
   except exists.  Moves the USERS buffer. */
static int other_admin(eng *e, u32 except, int *found)
{
    u32 r;
    u32 count;
    char v[ENG_MAXVAL];
    int rc;

    *found = 0;
    count = eng_reccount(e, ENG_T_USERS);
    for (r = 1; r <= count; r++) {
        if (r == except)
            continue;
        if ((rc = eng_rec_read(e, ENG_T_USERS, r)) != ENG_OK)
            return rc;
        if (dbf_is_deleted(eng_dbf(e, ENG_T_USERS)))
            continue;
        if ((rc = user_text(e, F_LEVEL, v)) != ENG_OK)
            return rc;
        if (level_of(v) != ENG_LVL_ADMIN)
            continue;
        if ((rc = user_text(e, F_STATUS, v)) != ENG_OK)
            return rc;
        if (v[0] == 'A' && v[1] == '\0') {
            *found = 1;
            return ENG_OK;
        }
    }
    return ENG_OK;
}

/* Refuse (ENG_ECONSTR naming field) a change that turns u from a live,
   active administrator into anything else when no other one exists;
   still is 1 when u stays one.  On ENG_OK u's record is in the buffer
   again. */
static int keep_admin(eng *e, const eng_urec *u, int still,
                      const char *field)
{
    int found;
    int rc;

    if (u->level != ENG_LVL_ADMIN || u->status != 'A' || still)
        return ENG_OK;
    rc = other_admin(e, u->recno, &found);
    if (rc == ENG_OK && !found)
        rc = eng_fail(e, ENG_ECONSTR, field, ENG_TX_LASTADM);
    if (rc == ENG_OK)
        rc = eng_rec_read(e, ENG_T_USERS, u->recno);
    return rc;
}

/* A value for field f of USERS. */
typedef struct {
    u16 f;
    const char *val;
} user_put;

/* Store the n values of r in the open edit, in order. */
static int put_rows(eng *e, const user_put *r, u16 n)
{
    u16 i;
    int rc;

    rc = ENG_OK;
    for (i = 0; rc == ENG_OK && i < n; i++)
        rc = eng_edit_putf(e, r[i].f, r[i].val);
    return rc;
}

/* The protocol pairs of USER ADD and USER SET, translated to fields:
   GROUP and GROUPNAME name GROUPNAME (both together are ENG_EARG naming
   the second), PASSWORD (ADD only) is taken out.  LEVEL and
   STATUS are checked here, as the field types alone allow 0..9 and any
   letter. */
#define USERPAIRS   4
#define NOFIELD     ((u16)0xFFFFU)  /* a key that names no field        */

typedef struct {
    user_put put[USERPAIRS];        /* the fields to store              */
    u16  n;
    const char *password;           /* NULL when not given              */
    u8   level;                     /* 0 when not given                 */
    char status;                    /* 0 when not given                 */
} user_args;

static int user_pairs(eng *e, const eng_pair *p, u16 n, int add,
                      user_args *a)
{
    u16 i;
    u16 j;
    u16 f;
    char c;

    a->n = 0;
    a->password = NULL;
    a->level = 0;
    a->status = 0;
    for (i = 0; i < n; i++) {
        f = NOFIELD;
        if (strcmp(p[i].key, k_level) == 0) {
            a->level = level_of(p[i].val);
            if (a->level == 0)
                return eng_fail(e, ENG_EARG, k_level, ENG_TX_BADLEVEL);
            f = F_LEVEL;
        } else if (strcmp(p[i].key, k_fullname) == 0) {
            f = F_FULLNAME;
        } else if (strcmp(p[i].key, k_group) == 0 ||
                   strcmp(p[i].key, eng_field_name(e, ENG_T_USERS,
                                                   F_GROUP)) == 0) {
            f = F_GROUP;
            for (j = 0; j < a->n; j++)
                if (a->put[j].f == F_GROUP)
                    return eng_fail(e, ENG_EARG, p[i].key, ENG_TX_NONE);
        } else if (strcmp(p[i].key, k_password) == 0 && add) {
            a->password = p[i].val;
            continue;
        } else if (strcmp(p[i].key, k_status) == 0 && !add) {
            c = p[i].val[0];
            c = (char)TXT_UPPER(c);
            if ((c != 'A' && c != 'D') || p[i].val[1] != '\0')
                return eng_fail(e, ENG_EARG, k_status, ENG_TX_STATUS);
            a->status = c;
            f = F_STATUS;
        }
        if (f == NOFIELD || a->n >= USERPAIRS)
            return eng_fail(e, ENG_EARG, p[i].key, ENG_TX_NOTUSET);
        a->put[a->n].f = f;
        a->put[a->n].val = p[i].val;
        a->n++;
    }
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * Logins and password changes.
 * --------------------------------------------------------------------- */

/* 1 when pw is the password of u.  A password outside the rules and a
   blank or malformed stored PWHASH never match, nor does any with u
   NULL.  Every call hashes pw once, with the salt k_salt0 when it cannot
   match, so the work is that of the password's length alone: a refusal
   takes as long as the check of a real account. */
static int pw_matches(const eng_urec *u, const char *pw)
{
    char hash[ENG_HASHLEN + 1];
    int ok;

    ok = u != NULL && eng_pw_valid(pw) && strlen(u->hash) == ENG_HASHLEN;
    eng_pw_hash(ok ? u->salt : k_salt0, pw, hash);
    ok = ok && strcmp(hash, u->hash) == 0;
    txt_wipe(hash, (u16)sizeof hash);
    return ok;
}

int eng_login_failed(eng *e, u32 id, const char *name, const char *fld,
                     int code)
{
    eng_audrow row;
    int rc;

    eng_set_user(e, "");
    row.action = ENG_A_FAILED;
    row.fld = fld != NULL ? fld :
              eng_field_name(e, ENG_T_USERS, F_USERNAME);
    row.oldval = NULL;
    row.newval = name != NULL ? name : "?";
    rc = eng_audit(e, ENG_T_USERS, id, &row);
    if (rc != ENG_OK)
        return rc;
    return eng_fail(e, code, NULL,
                    code == ENG_EDISABLED ? ENG_TX_DISABLED : ENG_TX_BADLOGIN);
}

/* The password is checked before the status, so only someone who knows
   it learns that an account is disabled.  The L row and the actor that
   remains afterwards carry the user who logged in; an F row carries no
   user, as a failed login logs the session out. */
int eng_user_login(eng *e, const char *name, const char *pw, int refuse,
                   eng_login *out)
{
    eng_urec u;
    char today[ENG_DATELEN + 1];
    eng_audrow row;
    int rc;

    memset(out, 0, sizeof *out);
    rc = eng_login_load(e, name, &u);
    if (rc == ENG_ENOTFOUND) {
        pw_matches(NULL, pw);
        return eng_login_failed(e, 0, NULL, NULL, ENG_EBADPW);
    }
    if (rc != ENG_OK)
        return rc;
    txt_copy(out->user, u.name, sizeof out->user);
    if (!pw_matches(refuse ? NULL : &u, pw))
        return eng_login_failed(e, u.id, u.name, NULL, ENG_EBADPW);
    if (u.status != 'A' || u.level == 0)
        return eng_login_failed(e, u.id, u.name, NULL, ENG_EDISABLED);

    eng_today(e, today);
    rc = eng_edit_begin(e, ENG_T_USERS, u.recno);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_finish(e, eng_edit_putf(e, F_LASTLOGIN, today), u.id);
    if (rc != ENG_OK)
        return rc;

    eng_set_user(e, u.name);
    row.action = ENG_A_LOGIN;
    row.fld = NULL;
    row.oldval = NULL;
    row.newval = NULL;
    rc = eng_audit(e, ENG_T_USERS, u.id, &row);
    if (rc != ENG_OK)
        return rc;
    out->level = u.level;
    out->mustchg = u.mustchg;
    txt_copy(out->fullname, u.fullname, sizeof out->fullname);
    return ENG_OK;
}

int eng_user_passwd(eng *e, const char *name, const char *oldpw,
                    const char *newpw)
{
    eng_urec u;
    char salt[ENG_SALTLEN + 1];
    int rc;

    rc = eng_user_load(e, name, &u);
    if (rc != ENG_OK)
        return rc;
    if (!pw_matches(&u, oldpw))
        return eng_fail(e, ENG_EBADPW, NULL, ENG_TX_WRONGPW);
    if (!eng_pw_valid(newpw))
        return eng_fail(e, ENG_EARG, k_new, ENG_TX_BADPW);
    if (strcmp(newpw, oldpw) == 0)
        return eng_fail(e, ENG_EARG, k_new, ENG_TX_SAMEPW);

    rc = make_salt(e, u.name, u.hash, salt);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_USERS, u.recno);
    if (rc != ENG_OK)
        return rc;
    return commit_password(e, put_password(e, salt, newpw, "F"), u.id);
}

/* ---------------------------------------------------------------------
 * Accounts.
 * --------------------------------------------------------------------- */

int eng_user_add(eng *e, const char *name, const eng_pair *p, u16 n)
{
    char nm[ENG_USERLEN + 1];
    char salt[ENG_SALTLEN + 1];
    char today[ENG_DATELEN + 1];
    user_args a;
    const char *fname;
    u32 recno;
    u32 id;
    int rc;

    id = 0;
    fname = eng_field_name(e, ENG_T_USERS, F_USERNAME);
    if (!user_name(name, nm))
        return eng_fail(e, ENG_EARG, fname, ENG_TX_BADUSER);
    if (strcmp(nm, ENG_SYSTEM) == 0)
        return eng_fail(e, ENG_EDUP, fname, ENG_TX_RESERVED);
    rc = eng_user_get(e, nm, &recno);
    if (rc == ENG_OK)
        return eng_fail(e, ENG_EDUP, fname, ENG_TX_USERDUP);
    if (rc != ENG_ENOTFOUND)
        return rc;
    eng_clear_error(e);
    if ((rc = user_pairs(e, p, n, 1, &a)) != ENG_OK)
        return rc;
    if (a.level == 0)
        return eng_fail(e, ENG_EARG, k_level, ENG_TX_NOLEVEL);
    if (a.password == NULL || !eng_pw_valid(a.password))
        return eng_fail(e, ENG_EARG, k_password, ENG_TX_BADPW);

    rc = make_salt(e, nm, NULL, salt);
    if (rc != ENG_OK)
        return rc;
    eng_today(e, today);
    rc = eng_edit_begin(e, ENG_T_USERS, 0);
    if (rc != ENG_OK)
        return rc;
    rc = put_rows(e, a.put, a.n);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_USERNAME, nm);
    if (rc == ENG_OK)
        rc = put_new(e, salt, a.password, today);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    /* The ID is reserved last, once nothing else can refuse the add. */
    if (rc == ENG_OK)
        rc = eng_seq_next(e, ENG_T_USERS, 1, &id);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_USERID, (i32)id);
    return eng_edit_finish(e, rc, id);
}

int eng_user_set(eng *e, const char *name, const eng_pair *p, u16 n)
{
    eng_urec u;
    user_args a;
    int rc;
    int still;

    if ((rc = eng_user_load(e, name, &u)) != ENG_OK)
        return rc;
    if ((rc = user_pairs(e, p, n, 0, &a)) != ENG_OK)
        return rc;
    still = (a.level == 0 || a.level == ENG_LVL_ADMIN) &&
            (a.status == 0 || a.status == 'A');
    rc = keep_admin(e, &u, still, a.level != 0 && a.level != ENG_LVL_ADMIN ?
                    k_level : k_status);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_USERS, u.recno);
    if (rc != ENG_OK)
        return rc;
    return eng_edit_finish(e, put_rows(e, a.put, a.n), u.id);
}

int eng_user_reset(eng *e, const char *name, const char *pw)
{
    eng_urec u;
    char salt[ENG_SALTLEN + 1];
    int rc;

    if ((rc = eng_user_load(e, name, &u)) != ENG_OK)
        return rc;
    if (!eng_pw_valid(pw))
        return eng_fail(e, ENG_EARG, k_password, ENG_TX_BADPW);
    rc = make_salt(e, u.name, u.hash, salt);
    if (rc != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_USERS, u.recno);
    if (rc != ENG_OK)
        return rc;
    return commit_password(e, put_password(e, salt, pw, k_true), u.id);
}

int eng_user_del(eng *e, const char *name)
{
    eng_urec u;
    int rc;

    if ((rc = eng_user_load(e, name, &u)) != ENG_OK)
        return rc;
    if ((rc = keep_admin(e, &u, 0, NULL)) != ENG_OK)
        return rc;
    rc = eng_edit_begin(e, ENG_T_USERS, u.recno);
    if (rc != ENG_OK)
        return rc;
    eng_edit_delete(e, 1);
    return eng_edit_commit(e, u.id);
}

int eng_user_scan(eng *e, eng_scan *s, const char *after)
{
    return eng_scan_open(e, s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, after);
}

int eng_user_seed(eng *e)
{
    /* The fields of the seeded user that are stored without hashing
       (the name upper-cased, as every user name is). */
    static const user_put seed[] = {
        { F_USERNAME, k_admin },
        { F_FULLNAME, "System Administrator" },
        { F_GROUP, k_admin },
        { F_LEVEL, "3" }
    };
    char today[ENG_DATELEN + 1];
    u32 id;
    int rc;

    id = 0;
    eng_today(e, today);
    rc = eng_edit_begin(e, ENG_T_USERS, 0);
    if (rc != ENG_OK)
        return rc;
    rc = put_rows(e, seed, (u16)(sizeof seed / sizeof seed[0]));
    if (rc == ENG_OK)
        rc = put_new(e, k_salt0, k_admin, today);
    /* The ID is reserved last, as by eng_user_add. */
    if (rc == ENG_OK)
        rc = eng_seq_next(e, ENG_T_USERS, 1, &id);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_USERID, (i32)id);
    return eng_edit_finish(e, rc, id);
}
