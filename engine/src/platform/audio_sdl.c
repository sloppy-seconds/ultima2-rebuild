/*
 * audio_sdl.c — sampled PC-speaker output.
 *
 * seam: speaker_freq (asm/5CD2_speaker_freq.asm). The original latches PIT
 * channel 2 with a 16-bit divisor; the engine hands the same divisor here, so
 * the only translation needed is divisor -> frequency, using the 8253's
 * 1.193182 MHz time base. The speaker was a square wave, so that is what is
 * generated — not a sine, and not a "beep" approximation.
 *
 * SDL's audio stream API is used rather than a callback: the tone is finite and
 * known in advance, so queueing pre-generated samples avoids a shared-state
 * callback and the locking it would need. SDL3 removed SDL_QueueAudio along with
 * the whole legacy SDL_OpenAudio path, so this now opens a device-backed stream
 * and pushes into it; the reasoning for avoiding a callback is unchanged.
 */

#ifdef U2_HAVE_SDL

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/ctx.h"

#define PIT_HZ 1193182L
#define AMP    9000          /* keep well inside int16 so overlapping tones clip */

typedef struct sdl_audio {
    SDL_AudioStream   *stream;
    SDL_AudioDeviceID  dev;
    int      open;
    int      rate;           /* the rate we asked the device for */
    long     queued;         /* samples queued so far, for diagnostics */
    unsigned tones;
} sdl_audio;

static int sdl_audio_init(u2_ctx *ctx)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        /* Silence is a legitimate outcome: a machine with no sound device must
         * not stop the game running, and the original ran fine with no speaker. */
        fprintf(stderr, "sdl audio: %s (continuing silent)\n", SDL_GetError());
        return 0;
    }
    sdl_audio *a = calloc(1, sizeof *a);
    if (!a) return 0;

    /* SDL3's SDL_AudioSpec is only what the data is — format, channels, rate.
     * `samples` and the callback are gone because the stream owns the buffering,
     * which is the same reason this file never wanted a callback in the first
     * place. */
    const SDL_AudioSpec spec = { .format = SDL_AUDIO_S16, .channels = 1, .freq = 22050 };
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &spec, NULL, NULL);
    if (!a->stream) {
        fprintf(stderr, "sdl audio: %s (continuing silent)\n", SDL_GetError());
        free(a);
        return 0;
    }
    a->dev   = SDL_GetAudioStreamDevice(a->stream);
    a->rate  = spec.freq;
    a->open  = 1;
    ctx->be_audio = a;
    /* A stream opened against a device starts paused; nothing plays until the
     * device is resumed. Easy to miss and it fails silently as total silence. */
    if (!SDL_ResumeAudioDevice(a->dev))
        fprintf(stderr, "sdl audio: %s (continuing silent)\n", SDL_GetError());
    return 0;
}

static void sdl_audio_tone(u2_ctx *ctx, uint16_t divisor, uint32_t ms)
{
    sdl_audio *a = ctx ? (sdl_audio *)ctx->be_audio : NULL;
    if (!a || !a->open || divisor == 0 || ms == 0) return;

    const long freq = PIT_HZ / (long)divisor;
    if (freq < 30 || freq > 11000) return;        /* outside a usable speaker range */

    long total = (long)a->rate * (long)ms / 1000L;
    if (total <= 0) return;
    if (total > (long)a->rate * 5) total = (long)a->rate * 5;   /* cap at 5 s */

    const long period = (long)a->rate / freq;     /* samples per half-cycle */
    if (period < 2) return;                       /* above the Nyquist limit */

    int16_t *buf = malloc((size_t)total * sizeof(int16_t));
    if (!buf) return;
    for (long i = 0; i < total; i++)
        buf[i] = ((i / period) & 1) ? (int16_t)-AMP : (int16_t)AMP;

    /* The old code queued against a hardcoded device id of 1, which was the legacy
     * SDL_OpenAudio handle. The stream is the handle now, so there is nothing to
     * get wrong. */
    if (!SDL_PutAudioStreamData(a->stream, buf, (int)(total * (long)sizeof(int16_t)))) {
        free(buf);
        return;
    }
    free(buf);
    a->queued += total;
    a->tones++;
}

/* Silence, so two notes with a gap stay two notes. Same stream, same format,
 * zero amplitude: the device plays the gap in real time while the engine's turn
 * has already moved on, which is what makes the original's step sound — two
 * thumps about 120 ms apart — reproducible without an animation clock. */
static void sdl_audio_silence(u2_ctx *ctx, uint32_t ms)
{
    sdl_audio *a = ctx ? (sdl_audio *)ctx->be_audio : NULL;
    if (!a || !a->open || ms == 0) return;

    long total = (long)a->rate * (long)ms / 1000L;
    if (total <= 0) return;
    if (total > (long)a->rate * 5) total = (long)a->rate * 5;

    int16_t *buf = calloc((size_t)total, sizeof(int16_t));
    if (!buf) return;
    if (!SDL_PutAudioStreamData(a->stream, buf, (int)(total * (long)sizeof(int16_t)))) {
        free(buf);
        return;
    }
    free(buf);
    a->queued += total;
}

static void sdl_audio_shutdown(u2_ctx *ctx)
{
    sdl_audio *a = ctx ? (sdl_audio *)ctx->be_audio : NULL;
    if (!a) return;
    /* Destroying the stream closes the device it opened, so there is no separate
     * SDL_CloseAudioDevice here to get out of order. */
    if (a->stream) SDL_DestroyAudioStream(a->stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(a);
    ctx->be_audio = NULL;
}

const u2_audio u2_audio_sdl = {
    .name = "sdl",
    .init = sdl_audio_init,
    .tone = sdl_audio_tone,
    .silence = sdl_audio_silence,
    .shutdown = sdl_audio_shutdown,
};

#endif /* U2_HAVE_SDL */
