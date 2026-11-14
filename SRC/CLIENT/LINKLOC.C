/*
 * LINKLOC.C - the LOCAL backend of the link (LINK.H): the engine inside
 * the program on a data directory, and one protocol session on it.
 *
 * This is the only client module that opens the engine.  It opens the
 * link and the engine and hands LINK.C, through the link's disp
 * (LINKINT.H), the dispatch of every request, which alone writes
 * responses with the protocol's writer.  A program without the engine
 * links LINKNOL.C in its place.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINKINT.H"

/* text into why (whysz bytes, cut to fit) when why is not NULL. */
static void say_why(char *why, u16 whysz, const char *text)
{
    if (why != NULL)
        txt_copy(why, text, whysz);
}

/* 1 if the directory of the prefix dir does not exist: opening a file in
   it fails with "path not found" rather than "file not found". */
static int no_dir(const char *dir)
{
    char probe[ENG_MAXPATH];
    int h;
    int rc;

    strcpy(probe, dir);
    strcat(probe, "ITEMS.DBF");
    rc = cf_open(probe, CF_READ | CF_DENYNONE, &h);
    if (rc == 0)
        cf_close(h);
    return rc == CF_ENOPATH;
}

/* The link's disp: the request to proto_dispatch, its output to
   link_sink. */
static int loc_disp(link *l, const char *line, u16 len)
{
    proto_out_init(&l->out, link_sink, NULL, l);
    return proto_dispatch(&l->host, &l->sess, line, len, &l->out);
}

int link_open_local(link **out, const char *dir, const link_clock *now,
                    char *why, u16 whysz)
{
    char path[LINK_MAXDIR + 1];
    eng_time t;
    link *l;
    size_t n;
    int addsep;
    int rc;

    *out = NULL;
    say_why(why, whysz, "");
    n = strlen(dir);
    addsep = n > 0 && dir[n - 1] != '\\' && dir[n - 1] != '/' &&
             dir[n - 1] != ':';
    if (n + (size_t)addsep > LINK_MAXDIR) {
        say_why(why, whysz, "data directory name too long");
        return ENG_EARG;
    }
    strcpy(path, dir);
    if (addsep) {
        if (strchr(path, '\\') != NULL)
            path[n] = '\\';
        else if (strchr(path, '/') != NULL)
            path[n] = '/';
        else
            path[n] = DOS_DIRSEP;
        path[n + 1] = '\0';
    }
    if (dos_set_handles(ENG_PROCHANDLES) != 0) {
        say_why(why, whysz, "not enough memory for the file handle table");
        return ENG_EINTERNAL;
    }
    l = (link *)malloc(sizeof *l);
    if (l == NULL) {
        say_why(why, whysz, "out of memory");
        return ENG_EINTERNAL;
    }
    memset(l, 0, sizeof *l);
    memset(&t, 0, sizeof t);
    if (now != NULL) {
        t.year = now->year;
        t.month = now->month;
        t.day = now->day;
        t.hour = now->hour;
        t.minute = now->minute;
        t.second = now->second;
    }
    rc = eng_open(&l->e, path, now != NULL ? &t : NULL, why, whysz);
    if (rc != ENG_OK) {
        free(l);
        if (no_dir(path))
            say_why(why, whysz, "no such directory: make it (MD) or name "
                    "another");
        return rc;
    }
    l->disp = loc_disp;
    l->host.e = l->e;
    sess_init(&l->sess, NULL);
    proto_acc_init(&l->acc);
    *out = l;
    return 0;
}
