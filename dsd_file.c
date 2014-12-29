/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "dsd.h"

void saveAmbe2450Data (dsd_opts * opts, dsd_state * state, char *ambe_d)
{
  int i, j, k;
  unsigned char b, buf[8];
  unsigned char err;

  err = (unsigned char) state->errs2;
  buf[0] = err;

  k = 0;
  for (i = 0; i < 6; i++) {
      b = 0;
      for (j = 0; j < 8; j++) {
          b = b << 1;
          b = b + ambe_d[k];
          k++;
      }
      buf[i+1] = b;
  }
  b = ambe_d[48];
  buf[7] = b;
  write(opts->mbe_out_fd, buf, 8);
}

void
processAudio (dsd_opts * opts, dsd_state * state)
{
  int i, n;
  float aout_abs, max, gainfactor, gaindelta, maxbuf;

  if (opts->audio_gain == 0.0f) {
      // detect max level
      max = 0;
      for (n = 0; n < 160; n++) {
          aout_abs = fabsf (state->audio_out_temp_buf[n]);
          if (aout_abs > max)
              max = aout_abs;
      }
      state->aout_max_buf[state->aout_max_buf_idx++] = max;
      if (state->aout_max_buf_idx > 24) {
          state->aout_max_buf_idx = 0;
      }

      // lookup max history
      for (i = 0; i < 25; i++) {
          maxbuf = state->aout_max_buf[i];
          if (maxbuf > max)
              max = maxbuf;
      }

      // determine optimal gain level
      if (max > 0.0f) {
          gainfactor = (30000.0f / max);
      } else {
          gainfactor = 50.0f;
      }
      if (gainfactor < state->aout_gain) {
          state->aout_gain = gainfactor;
          gaindelta = 0.0f;
      } else {
          if (gainfactor > 50.0f) {
              gainfactor = 50.0f;
          }
          gaindelta = gainfactor - state->aout_gain;
          if (gaindelta > (0.05f * state->aout_gain)) {
              gaindelta = (0.05f * state->aout_gain);
          }
      }
      gaindelta *= 0.00625f;
  } else {
      gaindelta = 0.0f;
  }

  if(opts->audio_gain >= 0) {
      // adjust output gain
      state->audio_out_temp_buf_p = state->audio_out_temp_buf;
      for (n = 0; n < 160; n++) {
          *state->audio_out_temp_buf_p = (state->aout_gain + ((float) n * gaindelta)) * (*state->audio_out_temp_buf_p);
          state->audio_out_temp_buf_p++;
      }
      state->aout_gain += (160.0f * gaindelta);
  }
}

void
writeSynthesizedVoice (dsd_opts * opts, dsd_state * state)
{
  short aout_buf[160];
  unsigned int n;

  state->audio_out_temp_buf_p = state->audio_out_temp_buf;

  for (n = 0; n < 160; n++) {
    if (*state->audio_out_temp_buf_p > 32767.0f) {
        *state->audio_out_temp_buf_p = 32767.0f;
    } else if (*state->audio_out_temp_buf_p < -32767.0f) {
        *state->audio_out_temp_buf_p = -32767.0f;
    }
    aout_buf[n] = (short) lrintf(*state->audio_out_temp_buf_p);
    state->audio_out_temp_buf_p++;
  }

  write(opts->wav_out_fd, aout_buf, 160 * sizeof(int16_t));
}

void
processMbeFrame (dsd_opts * opts, dsd_state * state, char ambe_fr[4][24])
{
  char ambe_d[49];

  if ((state->synctype == 6) || (state->synctype == 7)) {
      mbe_processAmbe3600x2400Framef (state->audio_out_temp_buf, &state->errs, &state->errs2, state->err_str, ambe_fr, ambe_d,
                                      &state->cur_mp, &state->prev_mp, &state->prev_mp_enhanced, opts->uvquality);
      if (opts->mbe_out_fd != -1) {
          saveAmbe2450Data (opts, state, ambe_d);
      }
  } else {
      mbe_processAmbe3600x2450Framef (state->audio_out_temp_buf, &state->errs, &state->errs2, state->err_str, ambe_fr, ambe_d,
                                      &state->cur_mp, &state->prev_mp, &state->prev_mp_enhanced, opts->uvquality);
      if (opts->mbe_out_fd != -1) {
          saveAmbe2450Data (opts, state, ambe_d);
      }
  }

  state->debug_audio_errors += state->errs2;
  processAudio (opts, state);
  if (opts->wav_out_fd != -1) {
      writeSynthesizedVoice (opts, state);
  }
}

void
closeMbeOutFile (dsd_opts * opts, dsd_state * state)
{
  char newfilename[64], new_path[1024];
  time_t tv_sec;
  struct tm timep;
  int result;

  if (opts->mbe_out_fd != -1) {
      tv_sec = opts->mbe_out_last_timeval;

      close(opts->mbe_out_fd);
      opts->mbe_out_fd = -1;
      gmtime_r(&tv_sec, &timep);
      sprintf (newfilename, "nac0-%04u-%02u-%02u-%02u:%02u:%02u-tg%u-src%u.amb",
               timep.tm_year + 1900, timep.tm_mon + 1, timep.tm_mday,
               timep.tm_hour, timep.tm_min, timep.tm_sec, state->talkgroup, state->radio_id);
      sprintf (new_path, "%s%s", opts->mbe_out_dir, newfilename);
      result = rename (opts->mbe_out_path, new_path);
  }
}

void
openMbeOutFile (dsd_opts * opts, dsd_state * state)
{
  struct timeval tv;

  gettimeofday (&tv, NULL);
  opts->mbe_out_last_timeval = tv.tv_sec;
  sprintf(opts->mbe_out_path, "%s%ld.amb", opts->mbe_out_dir, tv.tv_sec);

  if ((opts->mbe_out_fd = open (opts->mbe_out_path, O_WRONLY | O_CREAT, 0644)) < 0) {
      printf ("Error, couldn't open %s\n", opts->mbe_out_path);
      return;
  }

  // write magic
  write (opts->mbe_out_fd, ".amb", 4);
}

static const unsigned char static_hdr_portion[20] = {
   0x52, 0x49, 0x46, 0x46, 0xFF, 0xFF, 0xFF, 0x7F,
   0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
   0x10, 0x00, 0x00, 0x00
};

typedef struct _WAVHeader {
    uint16_t wav_id;
    uint16_t channels;
    uint32_t samplerate;
    uint32_t bitrate;
    uint32_t block_align;
    uint32_t pad0;
    uint32_t pad1;
} __attribute__((packed)) WAVHeader;

static void write_wav_header(int fd, uint32_t rate)
{
   WAVHeader w;
   write(fd, &static_hdr_portion, 20);

   w.wav_id = 1;
   w.channels = 1;
   w.samplerate = rate;
   w.bitrate = rate*2;
   w.block_align = 0x00100010;
   w.pad0 = 0x61746164;
   w.pad1 = 0x7fffffff;
   write(fd, &w, sizeof(WAVHeader));
}

void
openWavOutFile (dsd_opts *opts, const char *wav_out_file)
{
  if ((opts->wav_out_fd = open(wav_out_file, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0) {
    printf ("Error - could not open wav output file %s\n", wav_out_file);
    return;
  }
  //write_wav_header(opts->wav_out_fd, opts->wav_out_samplerate);
  write_wav_header(opts->wav_out_fd, 8000);
  //state->wav_out_bytes = 44;
}

void
closeWavOutFile (dsd_opts *opts)
{
  //size_t length = state->wav_out_bytes;
  close(opts->wav_out_fd);
}

