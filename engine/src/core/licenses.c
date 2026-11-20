/*
 * licenses.c — see licenses.h.
 *
 * Deliberately dumb: iterate a generated table and print it. Any cleverness here
 * (filtering, summarising, deciding an entry is "obvious") would be a judgement
 * about what a recipient is allowed to know, and that is not this file's job.
 */

#include "core/licenses.h"

#include <stddef.h>

/* Generated. Included here and nowhere else, so the table stays file-static. */
#include "core/licenses_data.h"

static void rule(FILE *out)
{
    fputs("--------------------------------------------------------------------------"
          "------------------------------\n", out);
}

void u2_licenses_print(FILE *out)
{
    if (!out) return;

    fputs("\nLicensing and attribution\n", out);
    fputs("=======================\n\n", out);
    fputs("This binary prints its own licence record because the release is a single\n"
          "file with no accompanying documents, and a recipient holding only the\n"
          "executable is still entitled to know what is in it.\n\n", out);

    for (size_t i = 0; i < (size_t)U2_LICENSE_COUNT; i++) {
        const u2_license_entry *e = &u2_license_table[i];
        rule(out);
        fprintf(out, "%s\n", e->title);
        fprintf(out, "  source in the repository: %s\n\n", e->origin);
        /* Chunks, not one fputs: see the note on the field in licenses_data.h.
         * Printed in order, so the output is the whole document as one run.
         */
        for (const char *const *chunk = e->text; *chunk; chunk++)
            fputs(*chunk, out);
        fputc('\n', out);
    }
    rule(out);
}
