/*
 * T_SRVSC2.C - the second part of T_SRVSCR (T_SRVSCR.C has the usage
 * and the list of checks, and the snapshot format): the snapshots, and
 * the checks of FULL, the session in slot 5, PORTS, MDA and the uptime
 * text longer than its field, which main of T_SRVSCR.C runs.
 */
#define TF_CMPFILE
#define COMPAT_UART
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "SRVCFG.H"
#include "MSG.H"
#include "SRVSCR.H"
#define TF_PART
#define TF_FILE "T_SRVSC2"
#include "TESTFW.H"
#include "T_SRVSCR.H"

#define NAMEMAX         13      /* an 8.3 name with its NUL             */
#define NATTR           16      /* attributes in one snapshot at most   */

/* The table's first row and the first port's row: SRVSCR.C's private
   SS_TAB and SS_PORTS + 1, repeated here; a change there fails the
   checks until these follow it. */
#define TAB_ROW         8
#define PORT_ROW        (TAB_ROW + CONN_MAX + 2)
#define UP_W            24      /* SRVSCR.C's SS_UPW, likewise          */
#define STAMPLEN        13      /* "=STAMP hhhh" CR LF                  */

static u16 g_nsnap[2];          /* snap's calls, colour and monochrome  */
static u16 g_ncmp;              /* snapshots compared with goldens      */

/* ---------------------------------------------------------------------
 * Snapshots.
 * --------------------------------------------------------------------- */
const char *row_text(u8 r, char *t)
{
    u8 cells[VID_ROWBYTES];
    int c;

    vid_getrow(r, cells);
    for (c = 0; c < VID_COLS; c++)
        t[c] = (char)cells[2 * c];
    for (c = VID_COLS; c > 0 && t[c - 1] == ' '; c--)
        ;
    t[c] = '\0';
    return t;
}

static int put(int h, const char *s)
{
    return cf_write(h, s, (u16)strlen(s));
}

/* Write the screen as the snapshot file path; 0 or a CF_E code. */
static int write_snap(const char *path, const char *name)
{
    u8 cells[VID_ROWBYTES];
    u8 attrs[NATTR];
    char t[VID_COLS + 5];       /* "|", a row, "|", CR LF, NUL          */
    u8 na;
    u8 r;
    u8 i;
    int c;
    int h;
    int rc;

    na = 0;
    rc = cf_create(path, &h);
    if (rc != 0)
        return rc;
    rc = put(h, "=SNAP ");
    if (rc == 0)
        rc = put(h, name);
    if (rc == 0)
        rc = put(h, "\r\n");
    t[0] = '|';
    for (r = 0; r < VID_ROWS && rc == 0; r++) {
        vid_getrow(r, cells);
        for (c = 0; c < VID_COLS; c++) {
            t[1 + c] = (char)cells[2 * c];
            if (cells[2 * c] == 0x00 || cells[2 * c] == 0x0A ||
                cells[2 * c] == 0x0D || cells[2 * c] == 0x1A)
                t[1 + c] = '?';
            for (i = 0; i < na && attrs[i] != cells[2 * c + 1]; i++)
                ;
            if (i == na && na < NATTR)
                attrs[na++] = cells[2 * c + 1];
        }
        strcpy(t + 1 + VID_COLS, "|\r\n");
        rc = put(h, t);
    }
    if (rc == 0)
        rc = put(h, "=ATTR");
    for (i = 0; i < na && rc == 0; i++) {
        t[0] = ' ';
        t[1] = (char)('A' + i);
        t[2] = '=';
        t[3] = txt_hexdig[attrs[i] >> 4];
        t[4] = txt_hexdig[attrs[i] & 15];
        t[5] = '\0';
        rc = put(h, t);
    }
    if (rc == 0)
        rc = put(h, "\r\n");
    t[0] = '|';
    for (r = 0; r < VID_ROWS && rc == 0; r++) {
        vid_getrow(r, cells);
        for (c = 0; c < VID_COLS; c++) {
            for (i = 0; i < na && attrs[i] != cells[2 * c + 1]; i++)
                ;
            t[1 + c] = (char)(i < na ? 'A' + i : '?');
        }
        strcpy(t + 1 + VID_COLS, "|\r\n");
        rc = put(h, t);
    }
    c = cf_close(h);
    return rc != 0 ? rc : c;
}

void snap(const char *name, int mono)
{
    char path[PATHMAX + NAMEMAX];
    char gold[PATHMAX + NAMEMAX];
    u8 r;
    u8 c;

    g_nsnap[mono]++;
    if (vid_mono() != mono)
        return;
    strcpy(gold, g_gdir);
    strcat(gold, name);
    strcat(gold, ".SNP");
    strcpy(path, g_dir);
    strcat(path, name);
    strcat(path, ".SNP");
    TF_EQ(vid_verify(&r, &c), 0);
    if (g_update) {
        TF_EQ(write_snap(gold, name), 0);
        return;
    }
    g_ncmp++;
    if (TF_EQ(write_snap(path, name), 0) &&
        TF_CHECK(tf_cmpfile(path, gold)))
        cf_remove(path);
}

/* The snapshots taken of class mono (0 colour, 1 monochrome) that a
   screen opened with flags shows: all of them or none (none too when it
   cannot open). */
static u16 shown(u8 flags, int mono)
{
    u16 n;

    if (vid_open(flags) != 0)
        return 0;
    n = vid_mono() == mono ? g_nsnap[mono] : 0;
    vid_close();
    return n;
}

/* ---------------------------------------------------------------------
 * The checks.
 * --------------------------------------------------------------------- */
static const char k_nosess[] = "No session is open.";

/* Every slot open, every session state, the longest name, station,
   user, data directory and READONLY list (the last two cut at the right
   edge), an idle time past a day; drawn through the BIOS with snow
   avoidance asked for, which the BIOS mode ignores. */
static void test_full(void)
{
    static const u8 states[] = {
        PROTO_S_NEW, PROTO_S_HELLO, PROTO_S_MUSTCHG, PROTO_S_READY,
        PROTO_S_CLOSED
    };
    srvcfg c;
    char sta[ENG_STALEN + 1];
    char user[ENG_USERLEN + 1];
    u16 i;

    c = g_cfg;
    memset(c.name, 'N', NB_NAMELEN - 1);
    c.name[NB_NAMELEN - 1] = '\0';
    memset(c.data, 'D', ENG_MAXDIR - 1);
    c.data[ENG_MAXDIR - 1] = '\\';
    c.data[ENG_MAXDIR] = '\0';
    c.readonly[0] = '\0';
    for (i = 0; i < SRVCFG_ROMAX; i++) {
        memset(sta, (char)('A' + i), ENG_STALEN);
        sta[ENG_STALEN] = '\0';
        if (i > 0)
            strcat(c.readonly, ",");
        strcat(c.readonly, sta);
    }
    memset(c.mailbox, 'M', ENG_MAXDIR - 1);
    c.mailbox[ENG_MAXDIR - 1] = '\\';
    c.mailbox[ENG_MAXDIR] = '\0';
    c.baud = UART_MAXBAUD;
    c.sessions = CONN_NETMAX;
    clear_conn();
    set_port(0, 1, UART_T_16550A, 0xFFFFu);
    set_port(1, 2, UART_T_16450, 0xFFFFu);
    for (i = 0; i < CONN_MAX; i++) {
        memset(sta, (char)('0' + i), ENG_STALEN);
        sta[ENG_STALEN] = '\0';
        memset(user, (char)('a' + i), ENG_USERLEN);
        user[ENG_USERLEN] = '\0';
        set_peer(i, (u8)(i + 1), sta, user, (u8)(i % 4),
                 states[i % (sizeof states / sizeof states[0])],
                 i == CONN_MAX - 1 ? 3 * 86400UL + 59 :
                 i == CONN_MAX - 2 ? 86400UL : 60UL * i);
    }
    strcpy(g_peers[CONN_MAX - 3].kind, "COM1");
    strcpy(g_peers[CONN_MAX - 2].kind, "COM2");
    strcpy(g_peers[CONN_MAX - 1].kind, "MBOX");
    g_npeers = CONN_MAX;
    g_uptime = 4000000UL;
    if (!TF_EQ(srvscr_open(VID_F_BIOS | VID_F_SNOW, &c), 0))
        return;
    TF_EQ(vid_snow_active(), 0);
    TF_EQ(srvscr_poll(700), SRVSCR_NONE);
    snap("FULL", 0);
    srvscr_close();
}

static void test_slot5(void)
{
    char t[VID_COLS + 1];
    u8 r;

    clear_conn();
    set_peer(0, 5, "LAB5", "ADMIN", ENG_LVL_ADMIN, PROTO_S_READY, 0);
    g_npeers = 1;
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(srvscr_poll(750), SRVSCR_NONE);
    TF_CHECK(strncmp(row_text(TAB_ROW, t), " 5 ", 3) == 0);
    TF_CHECK(strstr(t, "LAB5") != NULL);
    for (r = 0; r < VID_ROWS; r++)
        TF_CHECK(strstr(row_text(r, t), k_nosess) == NULL);
    srvscr_close();
}

/* COM2 alone, on an 8250, and a mailbox: the port's counts are redrawn
   with the uptime, not sooner. */
static void test_ports(void)
{
    srvcfg c;
    char t[VID_COLS + 1];

    c = g_cfg;
    strcpy(c.mailbox, "M:\\BOX\\");
    clear_conn();
    set_port(0, 2, UART_T_8250, 0);
    if (!TF_EQ(srvscr_open(0, &c), 0))
        return;
    TF_EQ(srvscr_poll(900), SRVSCR_NONE);
    TF_STREQ(row_text(PORT_ROW, t), " COM2                9600   8250    "
             "0        0        0        0");
    g_uinfo[0].overrun = 1;
    g_uinfo[0].framing = 22;
    g_uinfo[0].parity = 333;
    g_uinfo[0].lost = 0xFFFFu;
    TF_EQ(srvscr_poll(900 + TMR_SEC - 1), SRVSCR_NONE);
    TF_CHECK(strstr(row_text(PORT_ROW, t), "65535") == NULL);
    TF_EQ(srvscr_poll(900 + TMR_SEC), SRVSCR_NONE);
    TF_CHECK(strstr(row_text(PORT_ROW, t), "65535") != NULL);
    snap("PORTS", 0);
    srvscr_close();
}

static void test_mda(void)
{
    two_sessions();
    if (!TF_EQ(srvscr_open(VID_F_MDA, &g_cfg), 0))
        return;
    TF_EQ(srvscr_poll(800), SRVSCR_NONE);
    snap("MDA", 1);
    srvscr_close();
}

/* An uptime text longer than UP_W, from a message file of dir with the
   stamp of the real one and a short title, cut to UP_W at the right
   edge, the title beside it untouched. */
static void test_longup(void)
{
    char path[PATHMAX + NAMEMAX];
    char t[VID_COLS + 1];
    u16 got;
    int h;
    int rc;

    strcpy(path, g_mdir);
    strcat(path, MSG_SRVFILE);
    got = 0;
    if (TF_EQ(cf_open(path, CF_READ, &h), 0)) {
        TF_EQ(cf_read(h, t, STAMPLEN, &got), 0);
        cf_close(h);
    }
    strcpy(path, g_dir);
    strcat(path, MSG_SRVFILE);
    rc = got == STAMPLEN ? cf_create(path, &h) : CF_EINVAL;
    if (rc == 0) {
        rc = cf_write(h, t, STAMPLEN);
        if (rc == 0)
            rc = put(h, "1070 T ^1\r\n1071 Up ^1 since the start\r\n");
        cf_close(h);
    }
    clear_conn();
    g_uptime = 5;
    if (TF_EQ(rc, 0) && TF_EQ(msg_load(g_dir, MSG_SRVFILE), 0) &&
        TF_EQ(srvscr_open(0, &g_cfg), 0)) {
        TF_EQ(srvscr_poll(100), SRVSCR_NONE);
        row_text(0, t);
        TF_STREQ(t + VID_COLS - UP_W, "Up 0:00:05 since the sta");
        TF_EQ(strncmp(t, " T RISRV ", 9), 0);
        srvscr_close();
    }
    cf_remove(path);
}

void test_sc2(void)
{
    test_full();
    test_slot5();
    test_ports();
    test_mda();
    test_longup();
    TF_EQ(g_ncmp, g_update ? 0 : shown(0, 0) + shown(VID_F_MDA, 1));
}
