# SPDX-License-Identifier: GPL-3.0-or-later
NEWA = {}

NEWA["00483200"] = r'''
/* Decode mono OTNS ADPCM into 16-bit samples.
 *
 * This is the codec the .ADP voice files and the audio inside MORPH/*.3DM use.
 * It is IMA ADPCM with two departures from the textbook version, and both
 * matter - getting either wrong makes the predictor drift and buries the
 * speech under a DC offset of several thousand:
 *
 *   - the HIGH nibble of each byte is decoded first;
 *   - the step is scaled by (4*b2 + 2*b1 + b0) >> 2, with no unconditional
 *     `step >> 3` term. Textbook IMA always adds that eighth.
 *
 * Every decoded sample is written TWICE, so a stream encoded at 11025 Hz fills
 * a 22050 Hz output buffer. `outBytes` counts output bytes, and each pass of
 * the loop consumes one nibble and emits two samples, i.e. four bytes - hence
 * the (outBytes + 3) >> 2 iteration count.
 *
 * `src` is updated in place so the caller can resume, and `st` carries the
 * predictor, the step index and any half-consumed byte across calls.
 */
char **__cdecl Adpcm_DecodeMono(char **src, uint16_t *out, int outBytes, int16_t *st)
{
    AdpcmState *s = (AdpcmState *)st;
    char       *in = *src;
    int         index     = s->index;
    int         halfByte  = s->halfByte;
    int         predictor = s->predictor;
    int         heldByte  = s->heldByte;
    int         step      = ADPCM_STEP[index];
    unsigned    passes;
    int         nib, delta, raw;

    s->samplesOut += outBytes;
    if (outBytes <= 0)
        goto done;

    passes = (unsigned)(outBytes + 3) >> 2;
    do
    {
        if (halfByte)
        {
            raw = heldByte;              /* low nibble of the byte we kept */
        }
        else
        {
            raw = *in++;                 /* fresh byte: high nibble first  */
            heldByte = raw;
            ++s->bytesRead;
            raw >>= 4;
        }
        nib = raw & 0xF;

        index += ADPCM_INDEX[nib];
        if (index < 0) index = 0;
        else if (index > 88) index = 88;

        delta = 0;
        if (nib & 4) delta  = 4 * step;
        if (nib & 2) delta += 2 * step;
        if (nib & 1) delta += step;
        delta >>= 2;

        predictor = (nib & 8) ? predictor - delta : predictor + delta;
        if (predictor > 0x7FFF) predictor = 0x7FFF;
        else if (predictor < -32768) predictor = -32768;

        step = ADPCM_STEP[index];
        halfByte = !halfByte;

        *out++ = predictor;              /* written twice: 2x upsample */
        *out++ = predictor;
    }
    while (--passes);

done:
    s->predictor = predictor;
    s->index     = index;
    s->heldByte  = heldByte;
    s->halfByte  = halfByte;
    *src = in;
    return src;
}
'''

NEWA["00483340"] = r'''
/* Decode stereo OTNS ADPCM. One byte carries one sample for each channel: the
 * high nibble is the left channel, the low nibble the right, so there is never
 * a half-consumed byte to carry across calls.
 *
 * Same nibble expansion as the mono decoder, run over two independent
 * predictor/index pairs. The samples are interleaved L,R in `out`, which is
 * also why this one does not duplicate them - the two channels fill the space
 * the mono decoder fills by writing each sample twice.
 *
 * The state is the same AdpcmState; the right channel's step index lives at
 * +6, inside what the mono decoder treats as spare room.
 */
char *__cdecl Adpcm_DecodeStereo(char **src, uint16_t *out, int outBytes, int16_t *st)
{
    AdpcmState *s = (AdpcmState *)st;
    char       *in = *src;
    int   indexL = i8i(st, 2), indexR = i8i(st, 6);
    int   predL  = *st,        predR  = st[2];
    int   stepL  = ADPCM_STEP[indexL], stepR = ADPCM_STEP[indexR];
    unsigned passes;
    int   byte, nib, delta;

    s->samplesOut += outBytes;
    if (outBytes <= 0)
        goto done;

    passes = (unsigned)(outBytes + 3) >> 2;
    do
    {
        byte = *in++;
        ++s->bytesRead;

        /* ---- left: high nibble ---- */
        nib = (byte >> 4) & 0xF;
        indexL += ADPCM_INDEX[nib];
        if (indexL < 0) indexL = 0;
        else if (indexL > 88) indexL = 88;

        delta = 0;
        if (nib & 4) delta  = 4 * stepL;
        if (nib & 2) delta += 2 * stepL;
        if (nib & 1) delta += stepL;
        delta >>= 2;
        predL = (nib & 8) ? predL - delta : predL + delta;
        if (predL > 0x7FFF) predL = 0x7FFF;
        else if (predL < -32768) predL = -32768;
        stepL = ADPCM_STEP[indexL];
        *out++ = predL;

        /* ---- right: low nibble ---- */
        nib = byte & 0xF;
        indexR += ADPCM_INDEX[nib];
        if (indexR < 0) indexR = 0;
        else if (indexR > 88) indexR = 88;

        delta = 0;
        if (nib & 4) delta  = 4 * stepR;
        if (nib & 2) delta += 2 * stepR;
        if (nib & 1) delta += stepR;
        delta >>= 2;
        predR = (nib & 8) ? predR - delta : predR + delta;
        if (predR > 0x7FFF) predR = 0x7FFF;
        else if (predR < -32768) predR = -32768;
        stepR = ADPCM_STEP[indexR];
        *out++ = predR;
    }
    while (--passes);

done:
    u8i(st, 2) = indexL;
    u8i(st, 6) = indexR;
    *st  = predL;
    st[2] = predR;
    *src = in;
    return in;
}
'''
