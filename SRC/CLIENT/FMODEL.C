/*
 * FMODEL.C - the model of a data-entry form: values, checks and the
 * save.
 *
 * A form is one near-heap block: the client's struct form, which starts
 * with the form_model, then the current values and after them the values
 * they were loaded with, each field in a slot of form_valsz bytes at the
 * same offset in both sets.
 */
#define FMODEL_CALLS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "FMODEL.H"

/* The open form that has FT_SECRET fields, for form_wipe, or NULL. */
static form *form_secret;

/* Field i of the form's table. */
static const form_field *fld(const form_model *m, u16 i)
{
    return &m->def->fields[i];
}

u16 form_valsz(const form_field *d)
{
    if (d->type == FT_DATE)
        return FORM_DATELEN + 1;
    if (d->type == FT_MEMO && d->width < FORM_MEMOW)
        return FORM_MEMOW + 1;
    return (u16)(d->width + 1);
}

form *form_new(const form_def *def, u8 mode, u16 size)
{
    const form_field *d;
    form_model *m;
    char *b;
    u16 off[FORM_MAXFIELDS];
    size_t n;
    u16 vsz;
    u16 i;
    int secret;

    if (def->nfields > FORM_MAXFIELDS)
        return NULL;
    vsz = 0;
    secret = 0;
    for (i = 0; i < def->nfields; i++) {
        d = &def->fields[i];
        if (d->width > FORM_MAXVAL)
            return NULL;
        off[i] = vsz;
        vsz = (u16)(vsz + form_valsz(d));
        secret |= d->type == FT_SECRET;
    }
    n = (size_t)size + 2 * (size_t)vsz;
    b = (char *)malloc(n);
    if (b == NULL)
        return NULL;
    memset(b, 0, n);
    m = (form_model *)b;
    m->def = def;
    m->val = b + size;
    m->org = m->val + vsz;
    memcpy(m->off, off, def->nfields * sizeof off[0]);
    m->size = vsz;
    m->mode = mode;
    m->add = "ADD";
    if (secret)
        form_secret = (form *)b;
    return (form *)b;
}

void form_close(form *f)
{
    const form_model *m;
    u16 i;
    u16 n;

    if (f == NULL)
        return;
    if (f == form_secret)
        form_secret = NULL;
    m = (const form_model *)f;
    for (i = 0; i < m->def->nfields; i++)
        if (fld(m, i)->type == FT_SECRET) {
            n = form_valsz(fld(m, i));
            txt_wipe(m->val + m->off[i], n);
            txt_wipe(m->org + m->off[i], n);
        }
    free(f);
}

void form_wipe(void)
{
    form_close(form_secret);
}

u8 form_mode_get(form *f)
{
    return ((const form_model *)f)->mode;
}

void form_mode_set(form *f, u8 mode)
{
    ((form_model *)f)->mode = mode;
}

void form_set_add(form *f, const char *word)
{
    ((form_model *)f)->add = word;
}

int form_find(form *f, const char *name)
{
    const form_model *m;
    u16 i;

    m = (const form_model *)f;
    for (i = 0; i < m->def->nfields; i++)
        if (strcmp(fld(m, i)->name, name) == 0)
            return (int)i;
    return -1;
}

int form_key(form *f)
{
    const form_model *m;
    u16 i;

    m = (const form_model *)f;
    for (i = 0; i < m->def->nfields; i++)
        if (fld(m, i)->flags & FF_KEY)
            return (int)i;
    return -1;
}

char *form_val(form *f, u16 i)
{
    const form_model *m;

    m = (const form_model *)f;
    return m->val + m->off[i];
}

int form_load(form *f, const char *name, const char *val)
{
    const form_model *m;
    u16 n;
    int i;

    m = (const form_model *)f;
    i = form_find(f, name);
    if (i < 0)
        return -1;
    n = form_valsz(fld(m, (u16)i));
    txt_copy(m->val + m->off[i], val, n);
    txt_copy(m->org + m->off[i], val, n);
    return 0;
}

int form_kline(void *arg, const rq_line *ln)
{
    if (ln->kind == 'K' && ln->n == 2)
        form_load((form *)arg, ln->v[0], ln->v[1]);
    return 0;
}

int form_put(form *f, const char *name, const char *val)
{
    int i;

    i = form_find(f, name);
    if (i < 0)
        return -1;
    txt_copy(form_val(f, (u16)i), val,
             form_valsz(fld((const form_model *)f, (u16)i)));
    return 0;
}

const char *form_get(form *f, const char *name)
{
    int i;

    i = form_find(f, name);
    return i < 0 ? NULL : form_val(f, (u16)i);
}

void form_get_text(form *f, const char *name, char *d, u16 size)
{
    txt_copy(d, form_get(f, name), size);
}

/* 1 when field i differs from its loaded value; never for a memo
   field, whose count is no value to save. */
static int changed(const form_model *m, u16 i)
{
    return fld(m, i)->type != FT_MEMO &&
           strcmp(m->val + m->off[i], m->org + m->off[i]) != 0;
}

int form_dirty(form *f)
{
    const form_model *m;
    u16 i;

    m = (const form_model *)f;
    for (i = 0; i < m->def->nfields; i++)
        if (changed(m, i))
            return 1;
    return 0;
}

void form_saved(form *f)
{
    const form_model *m;

    m = (const form_model *)f;
    memcpy(m->org, m->val, m->size);
}

/* ---------------------------------------------------------------------
 * Checks.
 * --------------------------------------------------------------------- */
/* The message number of what is wrong with s as a number of d, or 0.
   A '-' before zero alone ("-0", "-0.00") is zero, as the engine
   stores it, so FF_NONNEG takes it. */
static u16 num_bad(const form_field *d, const char *s)
{
    u16 digits;
    u16 ints;
    u16 decs;
    int dot;
    int nonzero;
    const char *p;

    digits = 0;
    ints = 0;
    decs = 0;
    dot = 0;
    nonzero = 0;
    p = s;
    if (*p == '-')
        p++;
    for (; *p != '\0'; p++) {
        if (*p == '.' && !dot && d->dec > 0) {
            dot = 1;
        } else if (TXT_ISDIGIT(*p)) {
            digits++;
            nonzero |= *p != '0';
            if (dot)
                decs++;
            else
                ints++;
        } else {
            return MF_BADNUM;
        }
    }
    if (s[0] == '-' && nonzero && (d->flags & FF_NONNEG))
        return MF_NEGNUM;
    /* The width holds the sign, the integer digits, and with decimals
       the point and dec digits, as a dBASE N field does. */
    if (digits == 0 || decs > d->dec || strlen(s) > d->width ||
        (d->dec > 0 && (s[0] == '-') + ints + d->dec + 1 > d->width))
        return MF_BADNUM;
    return 0;
}

/* The next word of the space-separated list at *p: *w set to its first
   character, *p left just after it, and its length returned; 0 when the
   list has no more words. */
static u16 next_word(const char **p, const char **w)
{
    const char *s;
    u16 n;

    s = *p;
    n = 0;
    while (*s == ' ')
        s++;
    *w = s;
    while (s[n] != '\0' && s[n] != ' ')
        n++;
    *p = s + n;
    return n;
}

u16 form_choice_count(const char *choices)
{
    const char *w;
    u16 n;

    n = 0;
    while (next_word(&choices, &w) != 0)
        n++;
    return n;
}

/* A value that another program stored in lower case (the tables are
   dBASE III files) is no word of the list, so the field check refuses
   it. */
int form_choice_find(const char *choices, const char *word)
{
    const char *s;
    size_t n;
    u16 len;
    int at;

    n = strlen(word);
    at = 0;
    while ((len = next_word(&choices, &s)) != 0) {
        if (len == n && memcmp(s, word, n) == 0)
            return at;
        at++;
    }
    return -1;
}

u16 form_choice_num(const char *choices, const char *word)
{
    int at;

    at = form_choice_find(choices, word);
    return at < 0 ? 0 : (u16)at;
}

void form_choice_word(const char *choices, u16 i, char *out, u16 size)
{
    txt_word_at(choices, i, out, size);
}

u16 form_choice_name(const form_field *d, u16 i)
{
    return (u16)(d->dec != 0 ? d->label + d->dec + i : 0);
}

/* The message number of what is wrong with field i, or 0. */
static u16 check(const form_model *m, u16 i)
{
    const form_field *d;
    const char *v;

    d = fld(m, i);
    v = m->val + m->off[i];
    if (d->type == FT_MEMO || (d->flags & FF_RO))
        return 0;
    if (v[0] == '\0')
        return (d->flags & FF_REQ) ? MF_REQUIRED : 0;
    switch (d->type) {
    case FT_NUM:
        return num_bad(d, v);
    case FT_DATE:
        return txt_date_ok(v) ? 0 : MF_BADDATE;
    case FT_LOGIC:
        return (v[0] == 'T' || v[0] == 'F') && v[1] == '\0' ? 0 : MF_BADCHOICE;
    case FT_CHOICE:
        return d->choices != NULL && form_choice_find(d->choices, v) >= 0 ?
               0 : MF_BADCHOICE;
    default:
        return 0;
    }
}

int form_check(form *f, u16 *mf)
{
    const form_model *m;
    u16 i;
    u16 c;

    m = (const form_model *)f;
    for (i = 0; i < m->def->nfields; i++) {
        c = check(m, i);
        if (c != 0) {
            *mf = c;
            return (int)i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------
 * Saving.
 * --------------------------------------------------------------------- */
/* 1 when field i is a protocol field a save may send as a pair. */
static int sendable(const form_model *m, u16 i)
{
    const form_field *d;

    d = fld(m, i);
    return !(d->flags & (FF_RO | FF_LOCAL | FF_KEY)) && d->type != FT_MEMO;
}

/* Send q; on +OK mark the fields saved whose sent[] is 1. */
static int send_marked(form_model *m, link *l, rq *q, const u8 *sent,
                       rq_res *res)
{
    u16 i;
    int rc;

    rc = rq_send(l, q, NULL, NULL, res);
    if (rc == RQ_OK)
        for (i = 0; i < m->def->nfields; i++)
            if (sent[i])
                strcpy(m->org + m->off[i], m->val + m->off[i]);
    return rc;
}

int form_save(form *f, link *l, const char *table, char *id, u16 idsz,
              rq_res *res)
{
    form_model *m;
    rq q;
    char newid[LINK_IDLEN + 1];
    u8 sent[FORM_MAXFIELDS];    /* the fields the request carries       */
    u16 nf;
    u16 n;
    u16 i;
    int key;
    int rc;
    int added;

    m = (form_model *)f;
    nf = m->def->nfields;
    added = 0;
    m->partial = 0;
    /* RQ_OK and empty texts, as when nothing changed and nothing is
       sent. */
    memset(res, 0, sizeof *res);
    if (m->mode == FORM_NEW) {
        memset(sent, 0, sizeof sent);
        rq_begin(&q, table);
        rq_word(&q, m->add);
        key = form_key(f);
        if (key >= 0) {
            rq_word(&q, m->val + m->off[key]);
            sent[key] = 1;
        }
        for (i = 0; i < nf; i++)
            if (sendable(m, i) && (fld(m, i)->flags & FF_REQ)) {
                rq_pair(&q, fld(m, i)->name, m->val + m->off[i]);
                sent[i] = 1;
            }
        for (i = 0; i < nf; i++)
            if (sendable(m, i) && !sent[i] && changed(m, i) &&
                rq_trypair(&q, fld(m, i)->name, m->val + m->off[i]))
                sent[i] = 1;
        rc = send_marked(m, l, &q, sent, res);
        if (rc != RQ_OK)
            return rc;
        if (rq_okval(res, "ID", newid, (u16)sizeof newid)) {
            txt_copy(id, newid, idsz);
        } else if (key >= 0) {
            txt_copy(id, m->val + m->off[key], idsz);
        } else {
            /* An ADD without a key must name the new record. */
            res->code = RQ_EINTERNAL;
            return RQ_EINTERNAL;
        }
        m->mode = FORM_EDIT;
        added = 1;
    }
    for (;;) {
        memset(sent, 0, sizeof sent);
        n = 0;
        rq_begin(&q, table);
        rq_word(&q, "SET");
        rq_word(&q, id);
        for (i = 0; i < nf; i++)
            if (sendable(m, i) && changed(m, i)) {
                if (!rq_trypair(&q, fld(m, i)->name, m->val + m->off[i])) {
                    if (n != 0)
                        continue;
                    /* Too long even alone: rq_send refuses the request
                       (RQ_ETOOLONG) and the loop ends. */
                    q.over = 1;
                }
                sent[i] = 1;
                n++;
            }
        if (n == 0)
            break;
        rc = send_marked(m, l, &q, sent, res);
        if (rc != RQ_OK) {
            m->partial = (u8)added;
            return rc;
        }
    }
    return RQ_OK;
}

int form_err_field(form *f, const rq_res *res)
{
    if ((res->code == RQ_EARG || res->code == RQ_ENOTFOUND ||
         res->code == RQ_EDUP || res->code == RQ_ECONSTR) &&
        res->field[0] != '\0')
        return form_find(f, res->field);
    return -1;
}
