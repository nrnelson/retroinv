/*
 * SRVSCR.C - the server's status screen (SRVSCR.H).
 *
 * Layout, VID_ROWS x VID_COLS:
 *
 *   row SS_TOP                 the title (server name) and the uptime
 *   rows SS_INFO..SS_INFO+3    data directory, read-only stations,
 *                              sessions open and slots waiting
 *                              for a call
 *   row SS_HEAD                the column headings of the sessions
 *   rows SS_TAB..              one row per open session (conn_info)
 *   row SS_PORTS               the serial ports' column headings, or
 *                              none
 *   rows SS_PORTS+1..          one row per port served (tserial_info):
 *                              its name, baud rate, chip and errors
 *   row SS_MBOX                the mailbox directory, or none
 *   row SS_TRACE               the trace line
 *   row SS_BAR                 key hints, the shutdown question, or
 *                              the shutdown under way
 *
 * The frame, the settings and what the ports' rows say of each port
 * but its error counts are drawn once, at open; the rest is drawn at
 * most once every TMR_SEC ticks, as the uptime changes no faster, and
 * the shadow screen of CVIDEO.C writes only the cells that changed.
 * F5 and Ctrl-Break mark every cell changed (vid_invalidate), so the
 * next flush rewrites the whole screen, and redraw the rest at once.
 * Every text is a message of the server's message file (MSG.H);
 * attributes come from one of two palettes, the monochrome one keeping
 * to 07h, 0Fh and 70h, which every monochrome display shows distinctly.
 */
#define COMPAT_UART
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TSERIAL.H"
#include "SRVCFG.H"
#include "MSG.H"
#include "SRVSCR.H"

/* Messages, MSG_R_SRV range.  The state texts follow PROTO_S_NEW ..
   PROTO_S_CLOSED, the event texts CONN_EV_OPEN .. CONN_EV_CLOSE and the
   chip names UART_T_8250 .. UART_T_16550A, in the order of their
   numbers. */
#define MD_TITLE        1070    /* RETROINV server ^1 (its name)        */
#define MD_UPTIME       1071    /* Up ^1                                */
#define MD_DAYS         1072    /* ^1 days and ^2 (h:mm:ss)             */
#define MD_DATA         1073    /* label: data directory                */
#define MD_RO           1074    /* label: read-only stations            */
#define MD_NONE         1075    /* no read-only station                 */
#define MD_OPEN         1076    /* label: sessions open                 */
#define MD_LISTEN       1077    /* label: LISTENs waiting               */
#define MD_NOSESS       1078    /* the table when no session is open    */
#define MD_COLSLOT      1079    /* column headings, as cols lists them  */
#define MD_COLTYPE      1080
#define MD_COLSTA       1081
#define MD_COLUSER      1082
#define MD_COLLVL       1083
#define MD_COLSTATE     1084
#define MD_COLIDLE      1085
#define MD_STNEW        1086    /* session states                       */
#define MD_STHELLO      1087
#define MD_STMUSTCHG    1088
#define MD_STREADY      1089
#define MD_STCLOSED     1090
#define MD_TROFF        1091    /* the trace line, trace off            */
#define MD_TRON         1092    /* the trace line, trace on             */
#define MD_EVOPEN       1093    /* trace events: ^1 the slot            */
#define MD_EVLOGIN      1094
#define MD_EVREQ        1095
#define MD_EVCLOSE      1096
#define MD_HINT         1097    /* key hints                            */
#define MD_ASK          1098    /* ^1 sessions open: shut down?         */
#define MD_YESNO        1099    /* the letters answering MD_ASK         */
#define MD_STOPPING     1100    /* the shutdown under way               */
#define MD_SERIAL       1120    /* label: serial ports                  */
#define MD_MAILBOX      1121    /* label: mailbox                       */
#define MD_PORT         1122    /* COM^1, a port's row                  */
#define MD_COLBAUD      1123    /* the ports' headings, as pcols lists
                                   them                                 */
#define MD_COLCHIP      1124
#define MD_COLOVER      1125
#define MD_COLFRAME     1126
#define MD_COLPAR       1127
#define MD_COLLOST      1128
#define MD_C8250        1129    /* chips                                */
#define MD_C16450       1130
#define MD_C16550       1131
#define MD_C16550A      1132

/* The state and event texts run in step with their codes. */
extern char ss_states_fit[1];
extern char ss_states_fit[(MD_STCLOSED - MD_STNEW ==
                           PROTO_S_CLOSED - PROTO_S_NEW) ? 1 : 2];
extern char ss_events_fit[1];
extern char ss_events_fit[(MD_EVCLOSE - MD_EVOPEN ==
                           CONN_EV_CLOSE - CONN_EV_OPEN) ? 1 : 2];
extern char ss_chips_fit[1];
extern char ss_chips_fit[(MD_C16550A - MD_C8250 ==
                          UART_T_16550A - UART_T_8250) ? 1 : 2];

/* Rows. */
#define SS_TOP          0
#define SS_INFO         2
#define SS_HEAD         7
#define SS_TAB          (SS_HEAD + 1)
#define SS_PORTS        (SS_TAB + CONN_MAX + 1)
#define SS_MBOX         (SS_PORTS + CONN_SERMAX + 1)
#define SS_TRACE        (VID_ROWS - 3)
#define SS_BAR          (VID_ROWS - 1)

/* Every slot of the table, the ports and the mailbox have their rows
   above the trace line. */
extern char ss_rows_fit[1];
extern char ss_rows_fit[(SS_MBOX < SS_TRACE) ? 1 : 2];

/* Columns: the values of the settings rows, the uptime, and the table,
   each column as wide as its widest value. */
#define SS_VAL          21
#define SS_UP           (VID_COLS - SS_UPW)
#define SS_UPW          24      /* the uptime, right-aligned            */
#define SS_SLOT         1
#define SS_SLOTW        4
#define SS_KIND         (SS_SLOT + SS_SLOTW + 1)
#define SS_STA          (SS_KIND + PROTO_KINDLEN + 2)
#define SS_USER         (SS_STA + ENG_STALEN + 2)
#define SS_LVL          (SS_USER + ENG_USERLEN + 2)
#define SS_LVLW         5
#define SS_STATE        (SS_LVL + SS_LVLW + 2)
#define SS_STATEW       18
#define SS_IDLE         (SS_STATE + SS_STATEW + 2)
#define SS_IDLEW        (VID_COLS - SS_IDLE)
#define SS_BAUD         SS_VAL
#define SS_BAUDW        5       /* "38400"                              */
#define SS_CHIP         (SS_BAUD + SS_BAUDW + 2)
#define SS_CHIPW        6       /* "16550A"                             */
#define SS_ERR          (SS_CHIP + SS_CHIPW + 2)
#define SS_ERRW         7       /* a count, under its heading           */
#define SS_NERR         4       /* overruns, framing, parity, lost      */

/* A span of seconds as span writes it: "h:mm:ss" below a day, with the
   days put before it by MD_DAYS from a day on. */
#define SS_SPANLEN      MSG_MAXLEN
#define SS_DAY          86400UL
#define SS_HOUR         3600
#define SS_MIN          60

/* Palette roles, and the palettes: colour, monochrome. */
#define SR_NORM         0       /* text                                 */
#define SR_LABEL        1       /* labels and column headings           */
#define SR_BAR          2       /* the title and hint rows              */
#define SR_ASK          3       /* the shutdown question                */
#define SR_N            4

static const u8 pals[2][SR_N] = {
    { 0x17, 0x1E, 0x70, 0x4F },
    { 0x07, 0x0F, 0x70, 0x0F }
};

/* A column: where it starts, its width and its heading. */
typedef struct {
    u8  col;
    u8  w;
    u16 head;
} ss_col;

/* The column headings of the table. */
static const ss_col cols[] = {
    { SS_SLOT, SS_SLOTW, MD_COLSLOT },
    { SS_KIND, PROTO_KINDLEN, MD_COLTYPE },
    { SS_STA, ENG_STALEN, MD_COLSTA },
    { SS_USER, ENG_USERLEN, MD_COLUSER },
    { SS_LVL, SS_LVLW, MD_COLLVL },
    { SS_STATE, SS_STATEW, MD_COLSTATE },
    { SS_IDLE, SS_IDLEW, MD_COLIDLE }
};

/* The column headings of the ports' rows, the counts last. */
static const ss_col pcols[] = {
    { SS_BAUD, SS_BAUDW, MD_COLBAUD },
    { SS_CHIP, SS_CHIPW, MD_COLCHIP },
    { SS_ERR, SS_ERRW, MD_COLOVER },
    { SS_ERR + SS_ERRW + 2, SS_ERRW, MD_COLFRAME },
    { SS_ERR + 2 * (SS_ERRW + 2), SS_ERRW, MD_COLPAR },
    { SS_ERR + 3 * (SS_ERRW + 2), SS_ERRW, MD_COLLOST }
};
#define PC_ERR          2       /* index in pcols of the first count    */

static const srvcfg *ss_cfg;    /* the settings; NULL while closed      */
static const u8 *ss_pal;
static u32 ss_drawn;            /* now at the last redraw               */
static u8 ss_due;               /* redraw at the next poll              */
static u8 ss_trace;             /* the trace is on                      */
static u8 ss_ask;               /* the shutdown question is shown       */
static u8 ss_quit;              /* SRVSCR_QUIT has been returned        */

/* Write s at row, col into a field of w cells: cut to w, the rest
   spaces. */
static void field(u8 row, u8 col, u8 w, const char *s, u8 role)
{
    u16 n;

    n = (u16)strlen(s);
    if (n > w)
        n = w;
    vid_put(row, col, s, n, ss_pal[role]);
    vid_fill(row, (u8)(col + n), (u16)(w - n), ' ', ss_pal[role]);
}

/* A line from column 1 to the right edge; column 0 stays a margin. */
static void line(u8 row, const char *s, u8 role)
{
    field(row, 1, VID_COLS - 1, s, role);
}

/* The span of s seconds into out (SS_SPANLEN + 1 bytes). */
static void span(char *out, u32 s)
{
    char days[TXT_U32LEN + 1];
    char hms[TXT_U32LEN + 1];   /* "h:mm:ss", in txt_u32's room         */
    u16 k;

    k = txt_u32(hms, s % SS_DAY / SS_HOUR);
    hms[k] = ':';
    txt_u32w(hms + k + 1, s % SS_HOUR / SS_MIN, 2, '0');
    hms[k + 3] = ':';
    txt_u32w(hms + k + 4, s % SS_MIN, 2, '0');
    hms[k + 6] = '\0';
    if (s < SS_DAY) {
        strcpy(out, hms);
    } else {
        txt_u32(days, s / SS_DAY);
        strcpy(out, msg_sub(MD_DAYS, days, hms));
    }
}

/* The bottom row, margin included: the question, the shutdown under
   way, or the key hints. */
static void draw_bar(void)
{
    char n[TXT_U32LEN + 1];
    const char *s;
    u8 role;

    role = SR_BAR;
    if (ss_ask) {
        txt_u32(n, conn_count());
        s = msg_sub(MD_ASK, n, NULL);
        role = SR_ASK;
    } else {
        s = msg(ss_quit || conn_stopping() ? MD_STOPPING : MD_HINT);
    }
    vid_fill(SS_BAR, 0, 1, ' ', ss_pal[role]);
    line(SS_BAR, s, role);
}

/* The row of open session p. */
static void draw_peer(u8 row, const proto_peer *p)
{
    char t[SS_SPANLEN + 1];

    txt_u32(t, p->slot);
    field(row, SS_SLOT, SS_KIND - SS_SLOT, t, SR_NORM);
    field(row, SS_KIND, SS_STA - SS_KIND, p->kind, SR_NORM);
    field(row, SS_STA, SS_USER - SS_STA, p->station, SR_NORM);
    field(row, SS_USER, SS_LVL - SS_USER, p->user, SR_NORM);
    t[0] = '\0';
    if (p->level != 0)
        txt_u32(t, p->level);
    field(row, SS_LVL, SS_STATE - SS_LVL, t, SR_NORM);
    field(row, SS_STATE, SS_IDLE - SS_STATE,
          msg((u16)(MD_STNEW + p->state - PROTO_S_NEW)), SR_NORM);
    span(t, p->idle);
    field(row, SS_IDLE, SS_IDLEW, t, SR_NORM);
}

/* The error counts of the ports served. */
static void draw_errors(void)
{
    char a[TXT_U32LEN + 1];
    uart_info ui;
    u16 v[SS_NERR];
    u8 port;
    u8 i;
    u8 k;

    for (i = 0; tserial_info(i, &port, &ui) == TSER_OK; i++) {
        v[0] = ui.overrun;
        v[1] = ui.framing;
        v[2] = ui.parity;
        v[3] = ui.lost;
        for (k = 0; k < SS_NERR; k++) {
            txt_u32(a, v[k]);
            field((u8)(SS_PORTS + 1 + i), pcols[PC_ERR + k].col, SS_ERRW, a,
                  SR_NORM);
        }
    }
}

/* Everything that changes: the uptime, the counts, the table, the
   ports' error counts and the hint row. */
static void draw_live(u32 now)
{
    char a[SS_SPANLEN + 1];
    const char *s;
    proto_peer p;
    u16 n;
    u8 i;

    span(a, conn_uptime(now));
    s = msg_sub(MD_UPTIME, a, NULL);
    n = (u16)strlen(s);
    if (n > SS_UPW)
        n = SS_UPW;
    vid_fill(SS_TOP, SS_UP, (u16)(SS_UPW - n), ' ', ss_pal[SR_BAR]);
    vid_put(SS_TOP, (u8)(VID_COLS - n), s, n, ss_pal[SR_BAR]);
    txt_u32(a, conn_count());
    field(SS_INFO + 2, SS_VAL, VID_COLS - SS_VAL, a, SR_NORM);
    n = 0;
    for (i = 0; i < CONN_MAX; i++) {
        if (conn_state(i) == CONN_LISTEN)
            n++;
    }
    txt_u32(a, n);
    field(SS_INFO + 3, SS_VAL, VID_COLS - SS_VAL, a, SR_NORM);
    for (i = 0; i < CONN_MAX; i++) {
        if (conn_info(i, &p) == 0)
            draw_peer((u8)(SS_TAB + i), &p);
        else
            line((u8)(SS_TAB + i), i == 0 ? msg(MD_NOSESS) : "", SR_NORM);
    }
    draw_errors();
    draw_bar();
}

/* The headings of the n columns c on row. */
static void heads(u8 row, const ss_col *c, u16 n)
{
    for (; n > 0; n--, c++)
        field(row, c->col, c->w, msg(c->head), SR_LABEL);
}

/* The ports' rows but their error counts, and the mailbox's. */
static void draw_ports(void)
{
    char a[TXT_U32LEN + 1];
    uart_info ui;
    u8 port;
    u8 row;
    u8 i;

    line(SS_PORTS, msg(MD_SERIAL), SR_LABEL);
    for (i = 0; tserial_info(i, &port, &ui) == TSER_OK; i++) {
        row = (u8)(SS_PORTS + 1 + i);
        txt_u32(a, port);
        line(row, msg_sub(MD_PORT, a, NULL), SR_LABEL);
        txt_u32(a, ss_cfg->baud);
        field(row, SS_BAUD, SS_BAUDW, a, SR_NORM);
        field(row, SS_CHIP, SS_CHIPW,
              msg((u16)(MD_C8250 + ui.chip - UART_T_8250)), SR_NORM);
    }
    if (i != 0)
        heads(SS_PORTS, pcols, sizeof pcols / sizeof pcols[0]);
    else
        field(SS_PORTS, SS_VAL, VID_COLS - SS_VAL, msg(MD_NONE), SR_NORM);
    line(SS_MBOX, msg(MD_MAILBOX), SR_LABEL);
    field(SS_MBOX, SS_VAL, VID_COLS - SS_VAL,
          ss_cfg->mailbox[0] != '\0' ? ss_cfg->mailbox : msg(MD_NONE),
          SR_NORM);
}

/* The frame and the settings, over a cleared screen. */
static void draw_frame(void)
{
    u8 r;

    for (r = 0; r < VID_ROWS; r++) {
        vid_fill(r, 0, VID_COLS, ' ',
                 ss_pal[r == SS_TOP || r == SS_BAR ? SR_BAR : SR_NORM]);
    }
    line(SS_TOP, msg_sub(MD_TITLE, ss_cfg->name, NULL), SR_BAR);
    line(SS_INFO, msg(MD_DATA), SR_LABEL);
    line(SS_INFO + 1, msg(MD_RO), SR_LABEL);
    line(SS_INFO + 2, msg(MD_OPEN), SR_LABEL);
    line(SS_INFO + 3, msg(MD_LISTEN), SR_LABEL);
    field(SS_INFO, SS_VAL, VID_COLS - SS_VAL, ss_cfg->data, SR_NORM);
    field(SS_INFO + 1, SS_VAL, VID_COLS - SS_VAL,
          ss_cfg->readonly[0] != '\0' ? ss_cfg->readonly : msg(MD_NONE),
          SR_NORM);
    heads(SS_HEAD, cols, sizeof cols / sizeof cols[0]);
    draw_ports();
    line(SS_TRACE, msg(MD_TROFF), SR_NORM);
}

int srvscr_open(u8 vflags, const srvcfg *cfg)
{
    int rc;

    rc = vid_open(vflags);
    if (rc != 0)
        return rc;
    ss_cfg = cfg;
    ss_pal = pals[vid_mono()];
    ss_due = 1;
    ss_trace = 0;
    ss_ask = 0;
    ss_quit = 0;
    draw_frame();
    vid_flush();
    return 0;
}

/* Act on key k; SRVSCR_QUIT when it asks for the shutdown. */
static int on_key(u16 k)
{
    const char *yn;

    if (k == KEY_BREAK || k == KEY_F5) {
        vid_invalidate();
        ss_due = 1;
    }
    if (ss_ask) {
        yn = msg(MD_YESNO);
        if (k < KEY_EXT)
            k = (u16)TXT_UPPER(k);
        if (k == (u8)yn[0])
            ss_quit = 1;
        else if (k != (u8)yn[1] && k != KEY_ESC)
            return SRVSCR_NONE;
        ss_ask = 0;
        draw_bar();
        return ss_quit ? SRVSCR_QUIT : SRVSCR_NONE;
    }
    switch (k) {
    case KEY_F2:
        ss_trace = (u8)!ss_trace;
        line(SS_TRACE, msg(ss_trace ? MD_TRON : MD_TROFF), SR_NORM);
        break;
    case KEY_F3:
        if (ss_quit || conn_stopping())
            break;
        ss_ask = 1;
        draw_bar();
        break;
    default:
        break;
    }
    return SRVSCR_NONE;
}

int srvscr_poll(u32 now)
{
    int rc;

    rc = SRVSCR_NONE;
    if (ss_cfg == NULL)
        return SRVSCR_NONE;
    while (key_ready()) {
        if (on_key(key_read()) == SRVSCR_QUIT)
            rc = SRVSCR_QUIT;
    }
    if (ss_ask && conn_stopping()) {
        ss_ask = 0;
        ss_due = 1;
    }
    if (ss_due || now - ss_drawn >= TMR_SEC) {
        draw_live(now);
        ss_drawn = now;
        ss_due = 0;
    }
    vid_flush();
    return rc;
}

void srvscr_trace(u8 c, u8 ev, const char *text)
{
    char n[TXT_U32LEN + 1];

    if (ss_cfg == NULL || !ss_trace || ev < CONN_EV_OPEN ||
        ev > CONN_EV_CLOSE)
        return;
    txt_u32(n, (u32)c + 1);
    line(SS_TRACE, msg_sub((u16)(MD_EVOPEN + ev - CONN_EV_OPEN), n, text),
         SR_NORM);
}

void srvscr_close(void)
{
    vid_close();
    ss_cfg = NULL;
}
