/*
 * ENGUSER.H - the users, passwords and logins of the engine (USERS.C).
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGUSER_H
#define ENGUSER_H

/* ---------------------------------------------------------------------
 * Users and logins (USERS.C).
 *
 * Passwords are ENG_PWMIN..ENG_PWMAX characters 21..7E, compared
 * case-sensitively.  PWHASH is eng_mix128 over SALT followed by the
 * password; SALT is regenerated on every password change.  User names
 * are 1..ENG_USERLEN characters A-Z, 0-9, '_', upper-cased on input.
 * Levels: 1 read-only, 2 operator, 3 admin.  Deleted users do not exist
 * for any function here.  The name ENG_SYSTEM is reserved for the rows
 * eng_open writes and is never a user.
 *
 * Salt: the first ENG_SALTLEN hex digits of eng_mix128 over the clock as
 * YYYYMMDDHHMMSS (14 digits), the user name, the value
 * eng_seq_peek(e, ENG_T_AUDIT, ...) gives just before the change's
 * eng_edit_begin (in decimal; it is the AUD_ID of the change's first
 * audit row), and the old PWHASH (none for a new user).
 * --------------------------------------------------------------------- */
#define ENG_HASHROUNDS  64      /* final mixing rounds of eng_mix128    */
#define ENG_MAXSEED     64      /* longest eng_mix128 message           */

/* The 128-bit mix: ENG_HASHLEN upper-case hex digits and a NUL into
   out (ENG_HASHLEN + 1 bytes).  State words a b c d start as 67452301
   EFCDAB89 98BADCFE 10325476 (hex).  The message is padded with one
   byte 80, then zero bytes to a multiple of 4, then its unpadded length
   as a 32-bit little-endian word.  For each 32-bit little-endian word w
   of the padded message: a ^= w, then two rounds.  The state is then
   saved as t, ENG_HASHROUNDS more rounds are made, and t is added back
   word by word (a += t.a, ...).  A round, all arithmetic modulo 2^32,
   rotl a left rotation:
     a += b; d ^= a; d = rotl(d,16);  c += d; b ^= c; b = rotl(b,12);
     a += b; d ^= a; d = rotl(d, 8);  c += d; b ^= c; b = rotl(b, 7);
   The output is a, b, c, d, each as 8 hex digits, most significant
   first.  len is at most ENG_MAXSEED. */
void eng_mix128(const u8 *msg, u16 len, char *out);

/* PWHASH for a salt of ENG_SALTLEN characters and a password, into out
   (ENG_HASHLEN + 1 bytes). */
void eng_pw_hash(const char *salt, const char *pw, char *out);

/* 1 if pw is an acceptable password, else 0. */
int eng_pw_valid(const char *pw);

/* Check a login.  A malformed or unknown user name or a wrong password
   gives ENG_EBADPW, a disabled user ENG_EDISABLED; each is audited as F
   (REC_ID the user's ID or 0, FLD USERNAME, NEWVAL the user's stored
   name when the name names a live user, else "?", so text typed into
   the name field, such as a password, is never kept).  refuse nonzero
   checks pw as a password that never matches.  A login naming no user,
   or refused, reads and hashes as much as one with a wrong password,
   so that its time tells neither apart.  Success sets LASTLOGIN, audits
   L and fills *out; whatever the result, out->user is the stored name
   of the live user the name names, "" when it names none or its lookup
   failed. */
int eng_user_login(eng *e, const char *name, const char *pw, int refuse,
                   eng_login *out);

/* A login ticket's tag: the first ENG_TAGLEN hex digits of the PWHASH
   its user had when it was issued.  SALT, and so PWHASH, changes with
   every PASSWD and USER RESET, which thus revoke the user's tickets.
   eng_user_hash and eng_user_resume are in USERTK.C, which only the
   programs that serve a mailbox link. */
#define ENG_TAGLEN      8

/* The PWHASH of the live user name into hash (ENG_HASHLEN + 1 bytes);
   errors as eng_user_get's, and no audit row. */
int eng_user_hash(eng *e, const char *name, char *hash);

/* Check a login with a ticket of user name: valid is 0 when the store
   holds no such ticket, else tag holds the ENG_TAGLEN digits it was
   issued with.  No such ticket, a tag that is not the start of the
   user's PWHASH, a malformed or unknown user name give ENG_EBADPW, a
   disabled user ENG_EDISABLED; each is audited as F (REC_ID the user's
   ID or 0, FLD TICKET, NEWVAL as for eng_user_login).  Success fills
   *out as eng_user_login does, with no audit row and LASTLOGIN as it
   was, which thus stays the date of the last login with a password. */
int eng_user_resume(eng *e, const char *name, int valid, const char *tag,
                    eng_login *out);

/* Change the password of user name, checking oldpw: ENG_EBADPW if it is
   wrong; ENG_EARG naming NEW if newpw is invalid or equal to oldpw.
   Clears MUSTCHG; audited as one W row (FLD PWHASH, no values). */
int eng_user_passwd(eng *e, const char *name, const char *oldpw,
                    const char *newpw);

/* Load the live user name into the USERS buffer; *recno receives its
   record number (0 on failure).  ENG_ENOTFOUND for a malformed, unknown
   or reserved (ENG_SYSTEM) name. */
int eng_user_get(eng *e, const char *name, u32 *recno);

/* USER ADD: pairs LEVEL (required, 1..3), PASSWORD (required), FULLNAME,
   GROUP or GROUPNAME (stored as GROUPNAME; both is ENG_EARG).  MUSTCHG
   is set, STATUS is A.  ENG_EDUP if a live user has the name or it is
   ENG_SYSTEM. */
int eng_user_add(eng *e, const char *name, const eng_pair *p, u16 n);

/* USER SET: pairs LEVEL, FULLNAME, GROUP or GROUPNAME, STATUS (A or
   D).  ENG_ECONSTR if the change would leave no live, active level-3
   user.  A change of level or status takes effect at the user's next
   LOGIN. */
int eng_user_set(eng *e, const char *name, const eng_pair *p, u16 n);

/* USER RESET: new salt and hash for pw, MUSTCHG set; one W row. */
int eng_user_reset(eng *e, const char *name, const char *pw);

/* USER DEL: mark deleted; ENG_ECONSTR for the last active admin. */
int eng_user_del(eng *e, const char *name);

/* USER LIST: scan USERS1 in name order, continuing after the cursor
   text after (NULL or "" to start at the beginning). */
int eng_user_scan(eng *e, eng_scan *s, const char *after);

/* Append the ADMIN user: FULLNAME "System Administrator", GROUPNAME
   ADMIN, level 3, SALT 0000, password admin, MUSTCHG T. */
int eng_user_seed(eng *e);

/* ---------------------------------------------------------------------
 * What USERS.C shares with USERTK.C, which define ENG_USERS_SHARED
 * before they include this header; no other module reads it.
 * --------------------------------------------------------------------- */
#ifdef ENG_USERS_SHARED

/* The fields of a user record that the rules read. */
typedef struct {
    u32  recno;
    u32  id;                        /* USER_ID                          */
    u8   level;                     /* LEVEL, 0 when not 1..3           */
    char status;                    /* STATUS, ' ' when blank           */
    int  mustchg;
    char name[ENG_USERLEN + 1];
    char fullname[ENG_FULLNAMELEN + 1];
    char salt[ENG_SALTLEN + 1];
    char hash[ENG_HASHLEN + 1];
} eng_urec;

/* Look the live user name up and fill u; errors as eng_user_get's. */
int eng_user_load(eng *e, const char *name, eng_urec *u);

/* eng_user_load for a login: a name of no user still reads and parses
   a user record, the first, so that the login does the reads of one
   that names a user. */
int eng_login_load(eng *e, const char *name, eng_urec *u);

/* Audit a failed login as F (FLD fld, USERNAME when NULL) with no user
   as the actor, and record code.  NEWVAL is name, the stored name of the
   existing user the login named, or "?" (name NULL) when it named none:
   a user who types the password into the name field must not leave it
   in AUDIT.DBF, so text that is not a user name is never written. */
int eng_login_failed(eng *e, u32 id, const char *name, const char *fld,
                     int code);
#endif

#endif
