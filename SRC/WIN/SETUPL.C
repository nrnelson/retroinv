/*
 * SETUPL.C - the portable part of SETUP: the list of files, the copy of
 * a file, the directory and Program Manager's commands (SETUPL.H).
 *
 * Every copy goes through the window of one expansion's memory, su_work,
 * kept outside the stack, on which SETUP runs under USER's dialog.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <string.h>
#include "COMPAT.H"
#include "SETUPL.H"

#define SU_HEADLINES    3       /* lines of the list before its files   */
#define SU_WORDS        4       /* words of a file's line               */
#define SU_NAMELEN      8       /* characters of a name before its '.'  */
#define SU_EXTLEN       3       /* characters of its extension          */

static const char k_roles[] = INS_ROLES;
static const char k_dirpunct[] = "$%'-_@~`!{}^#&";
static const char k_noopts[] = ",()[]\"";
static const char k_add1[] = "[CreateGroup(" SU_NAME ")][AddItem(";
static const char k_add2[] = "," SU_NAME ")]";
const char su_k_delete[] = "[DeleteGroup(" SU_NAME ")]";

static lz_work su_work;

/* The end of the line at p, before its LF and any CR, and the start of
   the next line into *next. */
static const char *line_end(const char *p, const char *end,
                            const char **next)
{
    const char *e;

    for (e = p; e < end && *e != '\n'; e++)
        ;
    *next = e < end ? e + 1 : e;
    if (e > p && e[-1] == '\r')
        e--;
    return e;
}

int su_list_open(su_list *l, const char *buf, u16 n)
{
    int i;

    if (n >= INS_LISTMAX)
        return 0;
    l->p = buf;
    l->end = buf + n;
    for (i = 0; i < SU_HEADLINES; i++) {
        if (l->p == l->end)
            return 0;
        line_end(l->p, l->end, &l->p);
    }
    return 1;
}

int su_list_next(su_list *l, char *name, u8 *roles)
{
    const char *e;
    const char *w[SU_WORDS];
    const char *p;
    const char *r;
    int n;

    do {
        if (l->p == l->end)
            return 0;
        p = l->p;
        e = line_end(p, l->end, &l->p);
    } while (p == e);
    for (n = 0; n < SU_WORDS && p < e; n++) {
        w[n] = p;
        while (p < e && *p != ' ')
            p++;
        if (p < e)
            p++;
    }
    if (n < SU_WORDS || e == w[3] || e - w[3] >= DOS_FINDNAME ||
        memchr(w[3], ' ', (size_t)(e - w[3])) != NULL)
        return -1;
    memcpy(name, w[3], (size_t)(e - w[3]));
    name[e - w[3]] = '\0';
    if (strpbrk(name, INS_SEPS) != NULL)
        return -1;
    *roles = 0;
    for (p = w[0]; *p != ' '; p++) {
        r = strchr(k_roles, *p);
        if (*p != '\0' && r != NULL)
            *roles = (u8)(*roles | 1 << (r - k_roles));
    }
    return 1;
}

/* 1 when c may stand in a name of su_dir_ok. */
static int dir_char(char c)
{
    return TXT_ISALPHA(c) || TXT_ISDIGIT(c) ||
           (c != '\0' && strchr(k_dirpunct, c) != NULL);
}

/* Take the blanks off both ends of s in place; its start. */
static char *trim(char *s)
{
    char *e;

    while (*s == ' ')
        s++;
    for (e = s + strlen(s); e > s && e[-1] == ' '; e--)
        ;
    *e = '\0';
    return s;
}

int su_dir_ok(char *dir)
{
    char *s;
    char *p;
    int n;
    int ext;

    s = trim(dir);
    memmove(dir, s, strlen(s) + 1);
    for (p = dir; *p != '\0'; p++)
        *p = (char)TXT_UPPER(*p);
    if (!TXT_ISALPHA(dir[0]) || dir[1] != ':' || dir[2] != '\\' ||
        p - dir > SU_DIRMAX)
        return 0;
    for (p = dir + 2; *p == '\\'; ) {
        p++;
        for (n = 0; dir_char(*p); n++)
            p++;
        ext = -1;
        if (*p == '.')
            for (p++, ext = 0; dir_char(*p); ext++)
                p++;
        if (n == 0 || n > SU_NAMELEN || ext == 0 || ext > SU_EXTLEN)
            return 0;
    }
    return *p == '\0';
}

int su_cmd(char *cmd, const char *dir, const char *opts)
{
    const char *e;
    size_t n;

    while (*opts == ' ')
        opts++;
    for (e = opts + strlen(opts); e > opts && e[-1] == ' '; e--)
        ;
    n = strlen(dir) + 1 + sizeof SU_PROG - 1;
    if (e > opts)
        n += 1 + (size_t)(e - opts);
    if (n >= SU_CMDMAX)
        return 0;
    strcpy(cmd, dir);
    strcat(cmd, "\\" SU_PROG);
    if (e > opts) {
        n = strlen(cmd);
        cmd[n++] = ' ';
        for (; opts < e; opts++) {
            if ((unsigned char)*opts < ' ' || strchr(k_noopts, *opts))
                return 0;
            cmd[n++] = *opts;
        }
        cmd[n] = '\0';
    }
    return 1;
}

void su_dde_add(char *out, const char *cmd)
{
    strcpy(out, k_add1);
    strcat(out, cmd);
    strcat(out, k_add2);
}

void su_join(char *out, const char *dir, const char *name)
{
    size_t n;

    strcpy(out, dir);
    n = strlen(out);
    if (n == 0 || out[n - 1] != DOS_DIRSEP)
        out[n++] = DOS_DIRSEP;
    strcpy(out + n, name);
}

int su_copy(const char *src, const char *dir, const char *name, int lz)
{
    char from[DOS_PATHMAX];
    char to[DOS_PATHMAX];
    char tmp[DOS_PATHMAX];
    char packed[DOS_FINDNAME];
    int in;
    int out;
    int rc;
    int rc2;

    if (lz) {
        strcpy(packed, name);
        lz_name(packed);
        su_join(from, src, packed);
        lz = cf_exists(from) == 1;
    }
    if (!lz)
        su_join(from, src, name);
    su_join(to, dir, name);
    su_join(tmp, dir, SU_TEMP);
    rc = cf_open(from, CF_READ, &in);
    if (rc != 0)
        return rc;
    rc = cf_create(tmp, &out);
    if (rc == 0) {
        rc = lz_copy(&su_work, in, out, lz);
        rc2 = cf_close(out);
        if (rc == 0)
            rc = rc2;
        if (rc == 0) {
            rc = cf_remove(to);
            if (rc == CF_ENOENT)
                rc = 0;
        }
        if (rc == 0)
            rc = cf_rename(tmp, to);
        if (rc != 0)
            cf_remove(tmp);
    }
    cf_close(in);
    return rc;
}

int su_packed(const su_list *files, const char *src)
{
    char path[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    su_list l;
    u8 roles;

    l = *files;
    while (su_list_next(&l, name, &roles) > 0) {
        if ((roles & (INS_R_WIN | INS_R_LZ)) != (INS_R_WIN | INS_R_LZ))
            continue;
        lz_name(name);
        su_join(path, src, name);
        if (cf_exists(path) == 1)
            return 1;
    }
    return 0;
}
