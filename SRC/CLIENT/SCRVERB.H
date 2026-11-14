/*
 * SCRVERB.H - the verbs of the screens' Record menus: their numbers,
 * levels and texts, for tui_verbs and tui_record of TUI.H.
 *
 * A verb is numbered from 1 (SV_), in the order every Record menu lists
 * its verbs; its text is the message of the same rank from MW_OPEN on
 * (SCRVERB.MSG), which names its key, if it has one of its own.  A verb
 * without one has the key SK of its number, which reaches a screen only
 * from its Record menu.  A screen gives tui_record its verbs as a set,
 * SV_BIT of each.
 *
 * The screens with a Record menu read this header, defining TUI_VERBS
 * before TUI.H for SK, and INV.C, which hands the verbs to tui_verbs,
 * defining TUI_VERBTAB; the other files do not, for Watcom C 6.5's
 * compiler memory.
 */
#ifndef SCRVERB_H
#define SCRVERB_H

#include "COMPAT.H"
#include "TUI.H"

/* ---------------------------------------------------------------------
 * Numbers, keys and texts.
 * --------------------------------------------------------------------- */
#define SV_OPEN         1       /* Enter                                */
#define SV_EDIT         2       /* Ctrl-E                               */
#define SV_NEW          3       /* Ins                                  */
#define SV_DEL          4       /* Del                                  */
#define SV_DUP          5       /* Ctrl-D                               */
#define SV_PART         6       /* Ins                                  */
#define SV_MOVE         7
#define SV_INTO         8
#define SV_OUT          9
#define SV_END          10
#define SV_ITEM         11
#define SV_LEND         12      /* Ctrl-L                               */
#define SV_RES          13      /* Ctrl-R                               */
#define SV_USE          14      /* Ctrl-U                               */
#define SV_PARTS        15
#define SV_SVC          16
#define SV_LOANS        17
#define SV_USED         18
#define SV_SPLIT        19
#define SV_TOP          20
#define SV_BELOW        21
#define SV_NEXT         22      /* Ctrl-PgDn                            */
#define SV_PREV         23      /* Ctrl-PgUp                            */
#define SV_BACK         24      /* Esc                                  */

#define SK(v)           (TUI_VERBKEY + (v))     /* the key of verb v    */
#define SV_BIT(v)       (1UL << ((v) - 1))      /* verb v in a set      */

/* The verbs that need TUI_LVL_OPER (SV_OPER) and the texts: only with
   TUI_VERBTAB. */
#ifdef TUI_VERBTAB
#define SV_OPER         (SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | SV_BIT(SV_DEL) | \
                         SV_BIT(SV_DUP) | SV_BIT(SV_PART) | \
                         SV_BIT(SV_MOVE) | SV_BIT(SV_INTO) | \
                         SV_BIT(SV_OUT) | SV_BIT(SV_END) | \
                         SV_BIT(SV_LEND) | SV_BIT(SV_RES) | \
                         SV_BIT(SV_USE) | SV_BIT(SV_SPLIT))

#define MW_OPEN         710     /* Open, Enter                          */
#define MW_EDIT         711     /* Edit, Ctrl-E                         */
#define MW_NEW          712     /* New, Ins                             */
#define MW_DEL          713     /* Delete, Del                          */
#define MW_DUP          714     /* Duplicate, Ctrl-D                    */
#define MW_PART         715     /* Install part, Ins                    */
#define MW_MOVE         716     /* Move                                 */
#define MW_INTO         717     /* Install in                           */
#define MW_OUT          718     /* Take out                             */
#define MW_END          719     /* Return or release                    */
#define MW_ITEM         720     /* Open item                            */
#define MW_LEND         721     /* Lend, Ctrl-L                         */
#define MW_RES          722     /* Reserve, Ctrl-R                      */
#define MW_USE          723     /* Use with, Ctrl-U                     */
#define MW_PARTS        724     /* Parts                                */
#define MW_SVC          725     /* Service records                      */
#define MW_LOANS        726     /* Loans and holds                      */
#define MW_USED         727     /* Used with it                         */
#define MW_SPLIT        728     /* Split units                          */
#define MW_TOP          729     /* Not installed only                   */
#define MW_BELOW        730     /* Everything below                     */
#define MW_NEXT         731     /* Next view, Ctrl-PgDn                 */
#define MW_PREV         732     /* Previous view, Ctrl-PgUp             */
#define MW_BACK         733     /* Back, Esc                            */

extern char sv_texts_fit[1];
extern char sv_texts_fit[(MW_BACK - MW_OPEN + 1 == SV_BACK) ? 1 : 2];
#endif

#endif
