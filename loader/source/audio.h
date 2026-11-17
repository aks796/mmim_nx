/* audio.h -- the console's audio output (audout), for FMOD's mixed PCM
 *
 * Unity 6's FMOD writes through AAudio (aaudio.c), which hands each burst
 * here; the wait for a free output buffer paces the mixer.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef AUDIO_H
#define AUDIO_H

// Open the sink (idempotent). rate in Hz, channels 1 or 2. Returns 1 on success.
int nx_audio_init(int rate, int channels);

// Queue interleaved 16-bit PCM; n_shorts is the sample count (frames*channels).
void nx_audio_write(const void *pcm_s16, int n_shorts);

#endif
