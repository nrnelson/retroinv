/*
 * ENGNOTE.H - how a NOTES write treats a memo: appended a line, or
 * staged in parts and written whole (eng_edit_begin_note of ENGEDIT.H).
 *
 * Kept out of ENGINE.H, as the other per-module headers are, so that
 * only the engine's note modules, the dispatcher and their tests pay the
 * compiler memory for it (Watcom C 6.5 runs out of memory on the largest
 * files otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGNOTE_H
#define ENGNOTE_H

/* The how of eng_item_addnote, eng_loc_addnote, eng_maint_addnote and
   eng_edit_begin_note.  ENG_NOTE_ADD appends a line to the stored
   memo.  The others save a memo too long for one request whole:
   ENG_NOTE_NEW starts the stage of the session in the low bits
   (ENG_NOTE_TAG: its server slot, or 0 for a mailbox's and a local
   session, known by the station and user of eng_set_actor alone) with
   the text, ENG_NOTE_PART appends the text to that stage as a new
   line, and with ENG_NOTE_END the stage then becomes the memo in one
   edit.  ENG_NOTE_WRITES is 1 when how ends in an edit of the record,
   0 for a step that only stages. */
#define ENG_NOTE_ADD    0x00
#define ENG_NOTE_NEW    0x10
#define ENG_NOTE_PART   0x20
#define ENG_NOTE_END    0x40
#define ENG_NOTE_TAG    0x0F
#define ENG_NOTE_WRITES(how) \
    (((how) & (ENG_NOTE_NEW | ENG_NOTE_PART)) == 0 || ((how) & ENG_NOTE_END))
#define ENG_NOTESTAGE   ".&M"   /* a stage's name in the data directory:
                                   its table's name, this and the tag,
                                   or for tag 0 a digit hashed from the
                                   station, in hex (ITEMS.&M3)          */

#endif
