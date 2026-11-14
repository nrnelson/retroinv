/*
 * T_FFID.C - the record IDs of the forms INVWIN shows: FF_ID (FMODEL.H)
 * marks exactly the fields that hold one.
 *
 * usage: T_FFID
 *
 * A record's ID is a number of LINK_IDLEN digits without decimals, and
 * no other number of the forms is: in the tables of the item, service
 * record, loan and audit forms (scr_item_fields, scr_maint_fields,
 * scr_loan_fields, scr_audit_fields), FF_ID is set on every FT_NUM
 * field of width LINK_IDLEN and dec 0 and on no other field, and each
 * table but the audit filter's has one at least, its own ID.  FF_ID is
 * a flag of its own on the host, as in INVWIN: one bit, apart from the
 * other FF_ flags.
 *
 * Prints each failure and a summary line; exit status 0 when every
 * check passes, 1 when one fails.
 */
#include <stdio.h>
#include "COMPAT.H"
#include "LINK.H"
#include "FMODEL.H"
#include "SCRIFORM.H"
#include "SCRMFORM.H"
#include "SCRNFORM.H"
#include "SCRZFORM.H"
#include "TESTFW.H"

#define FF_OTHERS (FF_REQ | FF_RO | FF_NEWONLY | FF_KEY | FF_UPPER | \
                   FF_NONNEG | FF_LOCAL)

/* The IDs among the n fields of table f, named name: the count of
   fields marked FF_ID, each of them checked against its shape. */
static int ids(const char *name, const form_field *f, u16 n)
{
    u16 i;
    int is_id;
    int marked;
    int count;

    count = 0;
    for (i = 0; i < n; i++) {
        is_id = f[i].type == FT_NUM && f[i].width == LINK_IDLEN &&
                f[i].dec == 0;
        marked = (f[i].flags & FF_ID) != 0;
        if (!TF_EQ(marked, is_id))
            printf("  %s %s: FF_ID %d\n", name, f[i].name, marked);
        count += marked;
    }
    return count;
}

int main(void)
{
    tf_begin("T_FFID");
    TF_CHECK(FF_ID != 0);
    TF_EQ(FF_ID & (FF_ID - 1), 0);
    TF_EQ(FF_ID & FF_OTHERS, 0);
    TF_CHECK(ids("item", scr_item_fields, SCR_IF_COUNT) >= 1);
    TF_CHECK(ids("maint", scr_maint_fields, SCR_MF_COUNT) >= 2);
    TF_CHECK(ids("loan", scr_loan_fields, SCR_NF_COUNT) >= 2);
    TF_EQ(scr_item_def.nfields, SCR_IF_COUNT);
    TF_EQ(scr_maint_def.nfields, SCR_MF_COUNT);
    ids("audit", scr_audit_fields, SCR_AF_COUNT);
    TF_EQ(scr_audit_def.nfields, SCR_AF_COUNT);
    return tf_done();
}
