/* trace.c — seam call log (see debug/trace.h). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "debug/trace.h"

int u2_trace_open(u2_ctx *ctx, const char *out)
{
    if (!out || !*out || out[0] == '-') {
        ctx->trace.out = stderr;
    } else {
        FILE *f = fopen(out, "w");
        if (!f) return -1;
        ctx->trace.out = f;
    }
    ctx->trace.enabled = 1;
    return 0;
}

void u2_trace_close(u2_ctx *ctx)
{
    if (ctx->trace.out && ctx->trace.out != stderr)
        fclose(ctx->trace.out);
    ctx->trace.out = NULL;
    ctx->trace.enabled = 0;
}

void u2_trace(u2_ctx *ctx, const char *seam, const char *asmref,
              const char *fmt, ...)
{
    if (!ctx->trace.enabled || !ctx->trace.out)
        return;
    fprintf(ctx->trace.out, "trace: seam=%s asm=%s", seam, asmref);
    va_list ap;
    va_start(ap, fmt);
    if (fmt && *fmt) {
        fputc(' ', ctx->trace.out);
        vfprintf(ctx->trace.out, fmt, ap);
    }
    va_end(ap);
    fputc('\n', ctx->trace.out);
    /* Flushed, not buffered. stdio to a file is fully buffered, so a trace written
     * through this seam could only be read AFTER the process exited -- which makes it
     * a post-mortem and not an observation. The deploy now points a live play session's
     * trace at a bind-mounted path so the turn flow can be watched while the player is
     * in the world, and that only works if each line reaches the file when it is
     * written. Nothing that reads a trace depends on the timing: every gate greps the
     * file after the run ends, and this only makes the bytes arrive sooner. */
    fflush(ctx->trace.out);
}
