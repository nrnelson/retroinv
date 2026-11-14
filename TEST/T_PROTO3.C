/*
 * T_PROTO3.C - the third part of the T_PROTO test (see T_PROTO.C): a
 * fake login table for the host, its store of login tickets and its
 * throttle, and their directives.  The
 * store holds at most FT_MAX tickets; the n-th it issues is n in
 * decimal, PROTO_TKTLEN digits with leading zeros (a ticket's form), so
 * a script knows every ticket it is given.  It replaces a station's
 * ticket, takes the first free entry else the first one, and drops an
 * entry a redeem names for another user or station, as TICKET.H says;
 * it keeps the tick of each entry's last issue or redeem, but its
 * tickets never expire.  A file of its own to stay within the compiler
 * memory of Watcom C 6.5.
 */
#define PROTO_TICKETS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGUSER.H"
#include "PROTO.H"
#include "T_PROTO.H"

#define FT_MAX          4       /* tickets the fake store holds         */
#define NOTEDSZ         (ENG_USERLEN + TXT_U32LEN + 8)  /* @NOTED line  */

typedef struct {
    char t[PROTO_TKTLEN + 1];   /* "" for a free entry                  */
    char user[ENG_USERLEN + 1];
    char sta[ENG_STALEN + 1];
    char tag[ENG_TAGLEN + 1];
    u32  at;                    /* tick of the issue or last redeem     */
} fake_tkt;

static fake_tkt g_ft[FT_MAX];
static u32 g_ftmade;
static char g_refuse[ENG_USERLEN + 1];  /* the user @REFUSE named      */
static u32 g_refat;                     /* the tick it named the user  */
static char g_noted[NOTEDSZ];           /* "N user ok tick" of the last
                                           note, "" for none           */

static u8 ft_issue(u32 now, const char *user, const char *station,
                   const char *hash, char *t)
{
    u8 i;
    u8 k;

    k = FT_MAX;
    for (i = 0; i < FT_MAX; i++)
        if (g_ft[i].t[0] != '\0' && txt_ieq(g_ft[i].sta, station))
            k = i;
    for (i = 0; i < FT_MAX && k == FT_MAX; i++)
        if (g_ft[i].t[0] == '\0')
            k = i;
    if (k == FT_MAX)
        k = 0;
    txt_u32w(t, ++g_ftmade, PROTO_TKTLEN, '0');
    t[PROTO_TKTLEN] = '\0';
    strcpy(g_ft[k].t, t);
    txt_copy(g_ft[k].user, user, (u16)sizeof g_ft[k].user);
    txt_copy(g_ft[k].sta, station, (u16)sizeof g_ft[k].sta);
    txt_copy(g_ft[k].tag, hash, (u16)sizeof g_ft[k].tag);
    g_ft[k].at = now;
    return (u8)(k + 1);
}

static void ft_drop(u8 tk)
{
    if (tk >= 1 && tk <= FT_MAX)
        memset(&g_ft[tk - 1], 0, sizeof g_ft[0]);
}

static u8 ft_redeem(u32 now, const char *t, const char *user,
                    const char *station, char *tag)
{
    u8 i;

    for (i = 0; i < FT_MAX; i++) {
        if (g_ft[i].t[0] == '\0' || strcmp(g_ft[i].t, t) != 0)
            continue;
        if (!txt_ieq(g_ft[i].user, user) || !txt_ieq(g_ft[i].sta, station)) {
            ft_drop((u8)(i + 1));
            return 0;
        }
        memcpy(tag, g_ft[i].tag, ENG_TAGLEN);
        g_ft[i].at = now;
        return (u8)(i + 1);
    }
    return 0;
}

static void ft_drop_user(const char *user)
{
    u8 i;

    for (i = 0; i < FT_MAX; i++)
        if (g_ft[i].t[0] != '\0' && txt_ieq(g_ft[i].user, user))
            ft_drop((u8)(i + 1));
}

/* The fake throttle: it refuses @REFUSE's user from its tick on. */
static int ft_refused(u32 now, const char *user)
{
    return g_refuse[0] != '\0' && now >= g_refat && txt_ieq(user, g_refuse);
}

static void ft_note(u32 now, const char *user, int ok)
{
    strcpy(g_noted, "N ");
    strcat(g_noted, user);
    strcat(g_noted, ok ? " 1 " : " 0 ");
    txt_u32(g_noted + strlen(g_noted), now);
}

static proto_tickets g_fake = {
    ft_issue, ft_redeem, ft_drop, ft_drop_user, ft_refused, ft_note,
    proto_ticket_cmd
};

void tickets_reset(void)
{
    memset(g_ft, 0, sizeof g_ft);
    g_ftmade = 0;
    g_refuse[0] = '\0';
    g_noted[0] = '\0';
    g_host.tickets = NULL;
    g_host.now = 0;
}

int tickets_directive(const char *name, char *arg)
{
    char ln[PROTO_MAXRESP];
    u32 v;
    u8 i;

    if (strcmp(name, "@TICK") == 0 && arg != NULL) {
        if (!number(&arg, &v))
            return 0;
        g_host.now = v;
        return 1;
    }
    if (strcmp(name, "@REFUSE") == 0) {
        txt_copy(g_refuse, arg != NULL ? arg : "", (u16)sizeof g_refuse);
        g_refat = g_host.now;
        return 1;
    }
    if (strcmp(name, "@NOTED") == 0 && arg == NULL) {
        if (g_noted[0] != '\0') {
            strcat(g_noted, "\r\n");
            sink_write(&g_snk, g_noted, (u16)strlen(g_noted));
        }
        g_noted[0] = '\0';
        return 1;
    }
    if (arg != NULL)
        return 0;
    if (strcmp(name, "@TICKETS") == 0) {
        g_host.tickets = &g_fake;
        return 1;
    }
    if (strcmp(name, "@MBOX") == 0) {
        g_sess.tkok = 1;
        return 1;
    }
    if (strcmp(name, "@STORE") != 0)
        return 0;
    for (i = 0; i < FT_MAX; i++) {
        if (g_ft[i].t[0] == '\0')
            continue;
        strcpy(ln, "T ");
        strcat(ln, g_ft[i].t);
        strcat(ln, " ");
        strcat(ln, g_ft[i].user);
        strcat(ln, " ");
        strcat(ln, g_ft[i].sta);
        strcat(ln, " ");
        txt_u32(ln + strlen(ln), g_ft[i].at);
        strcat(ln, "\r\n");
        sink_write(&g_snk, ln, (u16)strlen(ln));
    }
    return 1;
}
