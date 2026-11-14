/*
 * PRINT.C - output jobs for the printer or a text file (PRINT.H).
 *
 * Every byte of a job, text and control codes alike, goes through the
 * job's buffer of PR_BUFLEN bytes, which is sent when it is full and
 * when the job ends.  A printer that does not take a byte stops the
 * send at that byte; the wait function decides between sending the
 * rest again from there (only the bytes not taken) and cancelling.
 * Once a job has stopped, by a cancel or a file error, everything
 * given to it later is dropped, so callers may go on producing output
 * and look at pr_failed when it suits them.
 */
#include <stddef.h>
#include <stdlib.h>
#define COMPAT_FIND
#include "COMPAT.H"
#include "PRINT.H"

#define ESC             0x1B

struct pr {
    pr_wait_fn wait;
    void *arg;
    int  h;                     /* the file unless dest is PR_PRINTER   */
    int  cferr;                 /* CF_E code of a file error            */
    u32  lines;                 /* lines begun (pr_lines)               */
    u16  n;                     /* bytes in buf                         */
    u8   bol;                   /* the next byte begins a line          */
    u8   dest;
    u8   type;
    u8   style;                 /* PR_ styles in effect                 */
    u8   failed;                /* PR_OK, or the PR_ code it stopped on */
    u8   buf[PR_BUFLEN];
};

/* The printer's answer to status, through the job's wait function:
   1 to try again, 0 to cancel. */
static int ask(pr *p, int status)
{
    if (p->wait == NULL)
        return 0;
    return (*p->wait)(p->arg, status) != 0;
}

/* Send the buffer. */
static void flush(pr *p)
{
    u16 off;
    u16 done;
    int rc;

    off = 0;
    if (p->n == 0 || p->failed != PR_OK) {
        p->n = 0;
        return;
    }
    if (p->dest != PR_PRINTER) {
        rc = cf_write(p->h, p->buf, p->n);
        if (rc != 0) {
            p->cferr = rc;
            p->failed = PR_EFILE;
        }
    } else {
        while (off < p->n) {
            rc = prn_write(p->buf + off, (u16)(p->n - off), &done);
            off = (u16)(off + done);
            if (rc != PRN_OK && !ask(p, rc)) {
                p->failed = PR_ECANCEL;
                break;
            }
        }
    }
    p->n = 0;
}

static void put(pr *p, u8 b)
{
    if (p->failed != PR_OK)
        return;
    if (p->n >= PR_BUFLEN)
        flush(p);
    p->buf[p->n++] = b;
}

/* ESC c, then arg unless it is negative; PR_EPSON jobs only. */
static void esc(pr *p, u8 c, int arg)
{
    if (p->type != PR_EPSON)
        return;
    put(p, ESC);
    put(p, c);
    if (arg >= 0)
        put(p, (u8)arg);
}

int pr_open(pr **out, u8 dest, u8 type, u8 port, const char *path,
            pr_wait_fn wait, void *arg, int *cferr)
{
    pr *p;
    int rc;

    *out = NULL;
    *cferr = 0;
    p = (pr *)malloc(sizeof *p);
    if (p == NULL)
        return PR_ENOMEM;
    p->wait = wait;
    p->arg = arg;
    p->h = -1;
    p->cferr = 0;
    p->lines = 0;
    p->n = 0;
    p->bol = 1;
    p->dest = dest;
    p->type = dest == PR_PRINTER ? type : (u8)PR_PLAIN;
    p->style = 0;
    p->failed = PR_OK;
    if (dest != PR_PRINTER) {
        rc = dest == PR_FILE ? cf_create(path, &p->h)
                             : cf_create_new(path, &p->h);
        if (rc != 0) {
            free(p);
            *cferr = rc;
            return PR_EFILE;
        }
        *out = p;
        return PR_OK;
    }
    for (;;) {
        rc = prn_open(port);
        if (rc == PRN_OK)
            break;
        if (!ask(p, rc)) {
            free(p);
            return PR_ECANCEL;
        }
    }
    esc(p, '@', -1);
    *out = p;
    return PR_OK;
}

void pr_form(pr *p, u8 lines)
{
    esc(p, 'C', lines);
}

void pr_style(pr *p, u8 style)
{
    u8 change;

    change = (u8)(p->style ^ style);
    if (change & PR_BOLD)
        esc(p, (u8)((style & PR_BOLD) ? 'E' : 'F'), -1);
    if (change & PR_WIDE)
        esc(p, 'W', (style & PR_WIDE) ? 1 : 0);
    p->style = style;
}

/* The bytes go straight into the buffer, a run at a time, as put
   would store them one by one: a full buffer is sent only when another
   byte follows. */
void pr_text(pr *p, const char *s, u16 n)
{
    u8 *d;
    u16 room;
    u8 b;

    if (n == 0)
        return;
    p->lines += p->bol;
    p->bol = 0;
    while (n > 0 && p->failed == PR_OK) {
        if (p->n >= PR_BUFLEN) {
            flush(p);
            continue;
        }
        room = (u16)(PR_BUFLEN - p->n);
        if (room > n)
            room = n;
        n = (u16)(n - room);
        d = p->buf + p->n;
        p->n = (u16)(p->n + room);
        while (room-- != 0) {
            b = (u8)*s++;
            *d++ = (u8)((b < 0x20 || b == 0x7F) ? '?' : b);
        }
    }
}

void pr_eol(pr *p)
{
    p->lines += p->bol;
    p->bol = 1;
    put(p, '\r');
    put(p, '\n');
}

void pr_ff(pr *p)
{
    p->lines++;
    p->bol = 1;
    put(p, '\f');
}

int pr_failed(pr *p)
{
    return p->failed;
}

u32 pr_lines(pr *p)
{
    return p->lines;
}

int pr_close(pr *p, int *cferr)
{
    int rc;

    *cferr = 0;
    if (p == NULL)
        return PR_OK;
    flush(p);
    if (p->dest != PR_PRINTER) {
        rc = cf_close(p->h);
        if (rc != 0 && p->failed == PR_OK) {
            p->cferr = rc;
            p->failed = PR_EFILE;
        }
    } else {
        prn_close();
    }
    rc = p->failed;
    *cferr = p->cferr;
    free(p);
    return rc;
}
