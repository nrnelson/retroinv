/*
 * T_NBDRN.C - unit test for nba_drain (CNBDRAIN.C) over the simulated
 * NetBIOS provider (FAKENB.C) and a key source.
 *
 * usage: T_NBDRN
 *
 * Checks:
 * - with no NCB kept it returns 0 at once and asks no key;
 * - NCBs the provider gives back while the wait runs end it with 0,
 *   the keys only peeked at;
 * - a key pressed while NCBs stay owned ends the wait: the key is
 *   read, and the NCBs still owned are counted.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "FAKENB.H"

#define KEY_AT          3       /* the peek that sees the key or the end */

/* The key source: counts the peeks and the reads; at the KEY_AT-th
   peek it returns 'X' when key is set, else deletes the name of the
   kept LISTENs, which gives their NCBs back. */
typedef struct {
    u16 peeks;
    u16 reads;
    int key;
} keys;

static u16 src(void *arg, int peek)
{
    keys *k;

    k = (keys *)arg;
    if (!peek) {
        k->reads++;
        return 'X';
    }
    if (++k->peeks < KEY_AT)
        return KEY_NONE;
    if (k->key)
        return 'X';
    nb_delete_name("SRV");
    return KEY_NONE;
}

/* Two LISTENs for "SRV" whose cancels the provider does not confirm,
   kept by nba_close until the name is deleted. */
static void keep_two(void)
{
    nba *s;
    u8 num;

    fnb_reset();
    TF_EQ(nb_present(), 1);
    TF_EQ(nb_add_name("SRV", &num), NB_OK);
    TF_EQ(nba_open(&s, 2), 0);
    TF_EQ(nba_listen(s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_listen(s, 1, "SRV", "*", 0, 0), NB_OK);
    fnb_stall(2);
    TF_EQ(nba_close(s), 2);
    TF_EQ(nba_stuck(), 2);
}

static void run(keys *k, int key)
{
    k->peeks = 0;
    k->reads = 0;
    k->key = key;
    key_set_source(src, k);
}

int main(void)
{
    keys k;

    tf_begin("T_NBDRN");
    cmem_init();
    fnb_reset();
    run(&k, 1);
    TF_EQ(nba_drain(), 0);
    TF_EQ(k.peeks, 0);
    TF_EQ(k.reads, 0);
    keep_two();
    run(&k, 0);
    TF_EQ(nba_drain(), 0);
    TF_EQ(k.peeks, KEY_AT);
    TF_EQ(k.reads, 0);
    keep_two();
    run(&k, 1);
    TF_EQ(nba_drain(), 2);
    TF_EQ(k.peeks, KEY_AT);
    TF_EQ(k.reads, 1);
    TF_EQ(nb_delete_name("SRV"), NB_OK);
    TF_EQ(nba_stuck(), 0);
    key_set_source(NULL, NULL);
    fnb_reset();
    return tf_done();
}
