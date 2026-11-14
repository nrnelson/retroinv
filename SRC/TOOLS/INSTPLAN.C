/*
 * INSTPLAN.C - what INSTALL copies, keeps and removes (INSTPLAN.H).
 *
 * Every decision of the installer is here, over the cf_ calls, so that
 * the host tests run it on directories; INSTALL.C only asks and tells.
 * Every file is copied through the window of one expansion's memory,
 * ins_lz, kept off the stack, which the paths of the nested copy calls
 * already take much of.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "INSTPLAN.H"

static const char k_sep[2] = { DOS_DIRSEP, '\0' };

static lz_work ins_lz;

/* Write a, b and c into out (DOS_PATHMAX bytes): INS_OK, or CF_EINVAL
   when they do not fit. */
static int cat3(char *out, const char *a, const char *b, const char *c)
{
    size_t n;

    n = strlen(a) + strlen(b) + strlen(c);
    if (n >= DOS_PATHMAX)
        return CF_EINVAL;
    strcpy(out, a);
    strcat(out, b);
    strcat(out, c);
    return INS_OK;
}

/* ---------------------------------------------------------------------
 * The list.
 * --------------------------------------------------------------------- */

/* Copy the next word of the line at *p, up to its end e, into w (size
   bytes) and step past it and the space after it: 1, or 0 when there is
   none or it does not fit. */
static int word(const char **p, const char *e, char *w, u16 size)
{
    const char *s;
    u16 n;

    s = *p;
    for (n = 0; s < e && *s != ' '; s++) {
        if (n + 1 >= size)
            return 0;
        w[n++] = *s;
    }
    w[n] = '\0';
    if (s < e)
        s++;
    *p = s;
    return n != 0;
}

/* The bits of the characters of w in letters (bit i for its letter i),
   or 0 when w holds another. */
static u8 bits(const char *w, const char *letters)
{
    const char *c;
    u8 b;

    for (b = 0; *w != '\0'; w++) {
        c = strchr(letters, *w);
        if (c == NULL)
            return 0;
        b |= (u8)(1 << (int)(c - letters));
    }
    return b;
}

/* Read the file line from p to e into l's next entry: 1, or 0 when it
   breaks the format. */
static int file_line(ins_list *l, const char *p, const char *e)
{
    ins_file *f;
    char w[2 * TXT_U32LEN + 2];
    const char *s;

    if (l->nfiles == INS_MAXFILES)
        return 0;
    f = &l->f[l->nfiles++];
    if (!word(&p, e, w, sizeof w))
        return 0;
    f->roles = bits(w, INS_ROLES);
    f->lz = (u8)((f->roles & INS_R_LZ) != 0);
    f->roles = (u8)(f->roles & ~INS_R_LZ);
    if (!word(&p, e, w, sizeof w))
        return 0;
    f->disks = bits(w, "1234");
    if (!word(&p, e, w, sizeof w))
        return 0;
    for (f->size = 0, s = w; TXT_ISDIGIT(*s); s++)
        f->size = f->size * 10 + (u32)(*s - '0');
    f->files = 0;
    if (*s == ',' && f->roles == INS_R_DATA) {
        f->files = txt_num(s + 1, 0xFFFFu);
        s = f->files != 0 ? "" : s;
    }
    f->todo = 0;
    return f->roles != 0 && f->disks != 0 &&
           f->disks < (1 << l->ndisks) && *s == '\0' &&
           (f->roles != INS_R_DATA || f->files != 0) &&
           word(&p, e, f->name, sizeof f->name) && p == e &&
           strpbrk(f->name, INS_SEPS) == NULL;
}

/* Read the text of n bytes at t, a list, into l. */
static int parse(ins_list *l, const char *t, u16 n)
{
    const char *end;
    const char *e;
    const char *p;
    u16 len;
    char w[DOS_LABELLEN + 1];
    int line;

    memset(l, 0, sizeof *l);
    end = t + n;
    for (line = 0; t < end; line++, t = e + 1) {
        e = memchr(t, '\n', (size_t)(end - t));
        if (e == NULL)
            return INS_EFORMAT;
        len = (u16)(e - t);
        if (len != 0 && e[-1] == '\r')
            len--;
        p = t;
        if (line == 0) {
            if (len == 0 || len > INS_TITLEMAX)
                return INS_EFORMAT;
            memcpy(l->title, t, len);
        } else if (line == 1) {
            if (e + 1 - (end - n) > INS_SETMAX)
                return INS_EFORMAT;
            if (!word(&p, t + len, w, sizeof w))
                return INS_EFORMAT;
            while (l->ndisks < INS_MAXDISKS &&
                   word(&p, t + len, l->label[l->ndisks], DOS_LABELLEN + 1))
                l->ndisks++;
            if (l->ndisks == 0 || p != t + len)
                return INS_EFORMAT;
            memcpy(l->set, end - n, (size_t)(e + 1 - (end - n)));
        } else if (line == 2) {
            if (len != 1 || *t < '1' || *t >= '1' + l->ndisks)
                return INS_EFORMAT;
            l->here = (u8)(*t - '1');
        } else if (!file_line(l, t, t + len)) {
            return INS_EFORMAT;
        }
    }
    return l->nfiles != 0 ? INS_OK : INS_EFORMAT;
}

/* Read up to max bytes of the file at path into buf, their count into
   *n: INS_OK or a CF_E code. */
static int slurp(const char *path, char *buf, u16 max, u16 *n)
{
    int h;
    int rc;

    rc = cf_open(path, CF_READ, &h);
    if (rc != 0)
        return rc;
    rc = cf_read(h, buf, max, n);
    cf_close(h);
    return rc;
}

/* A list cut at INS_LISTMAX bytes is refused by the parser: a whole
   one is shorter, as its lines are too few and too short to fill it,
   and the bytes of a longer one hold more file lines than it may. */
int ins_load(ins_list *l, const char *path)
{
    char buf[INS_LISTMAX];
    u16 n;
    int rc;

    rc = slurp(path, buf, sizeof buf, &n);
    if (rc == INS_OK)
        rc = parse(l, buf, n);
    return rc;
}

/* 1 when name ends in ".EXE". */
static int is_exe(const char *name)
{
    const char *dot;

    dot = strchr(name, '.');
    return dot != NULL && strcmp(dot, ".EXE") == 0;
}

u8 ins_roles(const ins_list *l)
{
    u8 r;
    int i;

    r = 0;
    for (i = 0; i < l->nfiles; i++)
        if (is_exe(l->f[i].name) && strcmp(l->f[i].name, INS_SELF) != 0)
            r |= l->f[i].roles;
    return (u8)(r & (INS_R_DOS | INS_R_WIN));
}

void ins_prog(const ins_list *l, u8 r, char *name)
{
    int i;

    name[0] = '\0';
    for (i = 0; i < l->nfiles; i++)
        if (l->f[i].roles == r && is_exe(l->f[i].name)) {
            strcpy(name, l->f[i].name);
            *strchr(name, '.') = '\0';
            return;
        }
}

/* ---------------------------------------------------------------------
 * The target directory.
 * --------------------------------------------------------------------- */

/* The longest name below a target: the data directory's and a file's. */
#define INS_SUBLEN      (sizeof INS_DATA + DOS_FINDNAME)

int ins_dir_ok(char *dir)
{
    char *s;
    int c;

    txt_ucopy(dir, dir, DOS_PATHMAX);
    if (!TXT_ISALPHA(dir[0]) || dir[1] != ':' || dir[2] != '\\' ||
        strlen(dir) + INS_SUBLEN >= DOS_PATHMAX)
        return 0;
    for (s = dir + 2; *s != '\0'; s++) {
        c = (unsigned char)*s;
        if (c <= ' ' || c == '*' || c == '?' || c == '/' ||
            (c == '\\' && (s[1] == '\\' || s[1] == '\0')))
            return 0;
    }
    return 1;
}

int ins_has_dir(const char *dir)
{
    dos_find f;
    char pat[DOS_PATHMAX];
    char name[DOS_FINDNAME];

    return cat3(pat, dir, k_sep, "*.*") != INS_OK ||
           dos_find_first(&f, pat, name) != CF_ENOPATH;
}

int ins_has_data(const char *dst)
{
    dos_find f;
    char dir[DOS_PATHMAX];
    char pat[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    int rc;

    rc = cat3(dir, dst, k_sep, INS_DATA);
    if (rc == INS_OK)
        rc = cat3(pat, dir, k_sep, "*.*");
    if (rc == INS_OK)
        rc = dos_find_first(&f, pat, name);
    return rc != CF_ENOENT && rc != CF_ENOPATH;
}

/* ---------------------------------------------------------------------
 * Installing.
 * --------------------------------------------------------------------- */

void ins_plan(ins_list *l, u8 roles)
{
    int i;

    l->roles = roles;
    for (i = 0; i < l->nfiles; i++)
        l->f[i].todo = (u8)((l->f[i].roles & roles & ~INS_R_KEEP) != 0);
}

int ins_keep(ins_list *l, const char *dst, ins_ui_fn ui)
{
    char path[DOS_PATHMAX];
    ins_file *f;
    int a;
    int i;

    for (i = 0; i < l->nfiles; i++) {
        f = &l->f[i];
        if (!f->todo || !(f->roles & INS_R_KEEP) ||
            cat3(path, dst, k_sep, f->name) != INS_OK ||
            cf_exists(path) == 0)
            continue;
        a = ui(INS_Q_REPLACE, f->name);
        if (a < 0)
            return INS_ECANCEL;
        f->todo = (u8)a;
    }
    return INS_OK;
}

/* n bytes rounded up to whole clusters of c bytes. */
static u32 clusters(u32 n, u32 c)
{
    return (n + c - 1) / c * c;
}

/* The bytes the file path takes in clusters of c bytes, 0 when it
   cannot be opened. */
static u32 held(const char *path, u32 c)
{
    u32 size;
    int h;

    if (cf_open(path, CF_READ, &h) != INS_OK)
        return 0;
    if (cf_size(h, &size) != INS_OK)
        size = 0;
    cf_close(h);
    return clusters(size, c);
}

int ins_room(const ins_list *l, const char *dst, u32 *need, u32 *have)
{
    char path[DOS_PATHMAX];
    const ins_file *f;
    u32 freed;
    u32 c;
    u8 drive;
    int rc;
    int i;

    *need = 0;
    drive = (u8)(dst[1] == ':' ? TXT_UPPER(dst[0]) - 'A' + 1 : 0);
    rc = dos_disk_free(drive, have);
    if (rc == INS_OK)
        rc = dos_disk_cluster(drive, &c);
    if (rc != INS_OK)
        return rc;
    if (c == 0)
        c = 1;
    freed = 0;
    for (i = 0; i < l->nfiles; i++) {
        f = &l->f[i];
        if (!f->todo)
            continue;
        if (f->roles == INS_R_DATA) {
            *need += c + f->size + (u32)f->files * (c - 1);
        } else {
            *need += clusters(f->size, c);
            if (cat3(path, dst, k_sep, f->name) == INS_OK)
                freed += held(path, c);
        }
    }
    *need = *need > freed ? *need - freed : 0;
    return *need > *have ? INS_EROOM : INS_OK;
}

/* Copy the file from to the file to, expanded when lz, removing to when
   the copy fails: INS_OK or the first code, the path it names into
   failed: from when it cannot be opened or is damaged, else to. */
static int copy_file(const char *from, const char *to, int lz,
                     char *failed)
{
    int in;
    int out;
    int rc;
    int rc2;

    rc = cf_open(from, CF_READ, &in);
    if (rc != INS_OK) {
        strcpy(failed, from);
        return rc;
    }
    rc = cf_create(to, &out);
    if (rc == INS_OK) {
        rc = lz_copy(&ins_lz, in, out, lz);
        rc2 = cf_close(out);
        if (rc == INS_OK)
            rc = rc2;
        if (rc != INS_OK)
            cf_remove(to);
    }
    cf_close(in);
    if (rc != INS_OK)
        strcpy(failed, rc == INS_EFORMAT ? from : to);
    return rc;
}

/* The path of the file or directory f in src into path, under its
   compressed name when f is marked so and src holds that name, as a
   release disk does, else under its own; whether it is compressed into
   *lz: INS_OK, or CF_EINVAL when the path does not fit. */
static int locate(char *path, const char *src, const ins_file *f, u8 *lz)
{
    char name[DOS_FINDNAME];

    *lz = 0;
    if (f->lz) {
        strcpy(name, f->name);
        lz_name(name);
        if (cat3(path, src, name, "") == INS_OK &&
            (f->roles == INS_R_DATA ? ins_has_dir(path) :
             cf_exists(path) == 1)) {
            *lz = 1;
            return INS_OK;
        }
    }
    return cat3(path, src, f->name, "");
}

/* Copy the file f, or for the data directory each file in it, from src
   to dst, telling ui first: INS_OK or the first code, the path it names
   into failed. */
static int copy_one(const char *src, const char *dst, const ins_file *f,
                    ins_ui_fn ui, char *failed)
{
    dos_find fd;
    char from[DOS_PATHMAX];
    char to[DOS_PATHMAX];
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    char base[DOS_FINDNAME];
    u8 lz;
    int rc;

    ui(INS_N_COPY, f->name);
    strcpy(failed, f->name);
    rc = locate(from, src, f, &lz);
    if (rc == INS_OK)
        rc = cat3(to, dst, k_sep, f->name);
    if (f->roles != INS_R_DATA)
        return rc == INS_OK ? copy_file(from, to, lz, failed) : rc;
    if (rc == INS_OK)
        rc = cat3(a, from, k_sep, "*.*");
    if (rc == INS_OK)
        rc = dos_find_first(&fd, a, base);
    while (rc == INS_OK) {
        rc = cat3(a, from, k_sep, base);
        if (rc == INS_OK)
            rc = cat3(b, to, k_sep, base);
        if (rc == INS_OK)
            rc = copy_file(a, b, lz, failed);
        if (rc != INS_OK)
            return rc;
        rc = dos_find_next(&fd, base);
    }
    return rc == CF_ENOENT ? INS_OK : rc;
}

/* 1 when the list at src names the release and set of l. */
static int same_set(const ins_list *l, const char *src)
{
    char path[DOS_PATHMAX];
    char head[INS_SETMAX];
    u16 n;
    u16 len;

    len = (u16)strlen(l->set);
    return cat3(path, src, INS_LIST, "") == INS_OK &&
           slurp(path, head, len, &n) == INS_OK && n == len &&
           memcmp(head, l->set, len) == 0;
}

/* Ask ui for disk d until the drive holds it: INS_OK or INS_ECANCEL. */
static int insert(const ins_list *l, int d, const char *src, u8 drive,
                  ins_ui_fn ui)
{
    char label[DOS_LABELLEN + 1];

    for (;;) {
        if (ui(INS_Q_DISK, l->label[d]) < 0)
            return INS_ECANCEL;
        if (dos_volume_label(drive, label) == 0 &&
            strcmp(label, l->label[d]) == 0 && same_set(l, src))
            return INS_OK;
        ui(INS_N_WRONG, label);
    }
}

static int remove_dir(const char *dir, char *failed);

/* Copy the sample data f into data, the data directory of dst, made
   first when missing; when the copy fails, or the directory cannot be
   made, what it copied and the directory go, so that the data
   directory is either whole or not there. */
static int copy_data(const char *src, const char *dst, const char *data,
                     const ins_file *f, ins_ui_fn ui, char *failed)
{
    char scratch[DOS_PATHMAX];
    int rc;

    rc = ins_has_dir(data) ? INS_OK : cf_mkdir(data);
    if (rc != INS_OK)
        strcpy(failed, data);
    else
        rc = copy_one(src, dst, f, ui, failed);
    if (rc != INS_OK)
        remove_dir(data, scratch);
    return rc;
}

/* 1 when src holds a file l is to copy under its compressed name. */
static int packed(const ins_list *l, const char *src)
{
    char path[DOS_PATHMAX];
    u8 lz;
    int i;

    for (i = 0; i < l->nfiles; i++)
        if (l->f[i].todo && locate(path, src, &l->f[i], &lz) == INS_OK &&
            lz)
            return 1;
    return 0;
}

int ins_copy(ins_list *l, const char *src, u8 drive, const char *dst,
             ins_ui_fn ui, char *failed)
{
    char path[DOS_PATHMAX];
    char data[DOS_PATHMAX];
    ins_file *f;
    int pass;
    int d;
    int k;
    int rc;
    int i;

    rc = cat3(path, dst, k_sep, "");
    strcpy(failed, dst);
    if (rc == INS_OK && txt_ieq(path, src))
        return packed(l, src) ? INS_EPACKED : INS_OK;
    if (rc == INS_OK)
        rc = cf_mkdir(dst);
    if (rc == CF_EACCES && ins_has_dir(dst))
        rc = INS_OK;
    if (rc == INS_OK)
        rc = cat3(data, dst, k_sep, INS_DATA);
    for (d = l->here; rc == INS_OK && d >= 0; ) {
        for (pass = 0; pass < 2; pass++)
            for (i = 0; rc == INS_OK && i < l->nfiles; i++) {
                f = &l->f[i];
                if (!f->todo || !(f->disks & (1 << d)) ||
                    (f->roles == INS_R_DATA) != pass)
                    continue;
                rc = pass ? copy_data(src, dst, data, f, ui, failed) :
                     copy_one(src, dst, f, ui, failed);
                f->todo = 0;
            }
        for (d = -1, i = 0; rc == INS_OK && i < l->nfiles; i++) {
            f = &l->f[i];
            for (k = 0; f->todo && !(f->disks & (1 << k)); k++)
                ;
            if (f->todo && (d < 0 || k < d))
                d = k;
        }
        if (d >= 0)
            rc = insert(l, d, src, drive, ui);
    }
    if (rc == INS_OK && (l->roles & (INS_R_ALONE | INS_R_SERVER)) &&
        !ins_has_dir(data)) {
        rc = cf_mkdir(data);
        if (rc != INS_OK)
            strcpy(failed, data);
    }
    return rc;
}

/* ---------------------------------------------------------------------
 * Removing.
 * --------------------------------------------------------------------- */

/* Remove the file path; a missing one is no error. */
static int remove_file(const char *path, char *failed)
{
    int rc;

    rc = cf_remove(path);
    if (rc == CF_ENOENT)
        return INS_OK;
    if (rc != INS_OK)
        strcpy(failed, path);
    return rc;
}

/* Remove every file of the directory dir and then dir. */
static int remove_dir(const char *dir, char *failed)
{
    dos_find f;
    char pat[DOS_PATHMAX];
    char path[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    int rc;

    rc = cat3(pat, dir, k_sep, "*.*");
    if (rc == INS_OK)
        rc = dos_find_first(&f, pat, name);
    while (rc == INS_OK) {
        rc = cat3(path, dir, k_sep, name);
        if (rc == INS_OK)
            rc = remove_file(path, failed);
        if (rc == INS_OK)
            rc = dos_find_next(&f, name);
    }
    if (rc == CF_ENOENT)
        rc = cf_rmdir(dir);
    if (rc != INS_OK && failed[0] == '\0')
        strcpy(failed, dir);
    return rc;
}

int ins_remove(const ins_list *l, const char *dst, ins_ui_fn ui,
               char *failed)
{
    char path[DOS_PATHMAX];
    int data;
    int rc;
    int i;

    failed[0] = '\0';
    rc = cat3(path, dst, k_sep, INS_DATA);
    data = rc == INS_OK && ins_has_dir(path) ? ui(INS_Q_DATA, path) : 0;
    if (data < 0)
        return INS_ECANCEL;
    for (i = 0; rc == INS_OK && i <= l->nfiles; i++) {
        if (i < l->nfiles && (l->f[i].roles == INS_R_DATA ||
                              strcmp(l->f[i].name, INS_LIST) == 0))
            continue;
        rc = cat3(path, dst, k_sep, i < l->nfiles ? l->f[i].name :
                  INS_LIST);
        if (rc == INS_OK)
            rc = remove_file(path, failed);
    }
    if (rc == INS_OK && data) {
        rc = cat3(path, dst, k_sep, INS_DATA);
        if (rc == INS_OK)
            rc = remove_dir(path, failed);
    }
    if (rc == INS_OK && cf_rmdir(dst) != INS_OK)
        ui(INS_N_LEFT, dst);
    if (rc != INS_OK && failed[0] == '\0')
        strcpy(failed, dst);
    return rc;
}
