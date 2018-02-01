/* variantProjector -- use sequence alignments and indel shifting to transform genomic variants
 * to transcript/CDS and protein variants.  Compute sufficient information to predict functional
 * effects on proteins and to form HGVS g., n., and if applicable c. and p. terms. */

/* Copyright (C) 2017 The Regents of the University of California
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "indelShift.h"
#include "variantProjector.h"

static enum vpTxRegion complementRegion(enum vpTxRegion regionIn)
/* Reverse the +-strand-computed region for a transcript on the - strand. */
{
enum vpTxRegion regionOut = regionIn;
if (regionIn == vpUpstream)
    regionOut = vpDownstream;
else if (regionIn == vpDownstream)
    regionOut = vpUpstream;
return regionOut;
}

INLINE void swapUint(uint *pA, uint *pB)
/* Swap the values of a and b */
{
uint tmp = *pA;
*pA = *pB;
*pB = tmp;
}

static void vpTxPosRc(struct vpTxPosition *txPos, uint txSize)
/* Reverse/complement all components of txPos. */
{
txPos->region = complementRegion(txPos->region);
txPos->txOffset = txSize - txPos->txOffset;
// No change to distances -- except for introns where we swap 5' / 3':
if (txPos->region == vpIntron)
    {
    txPos->intron3TxOffset = txSize - txPos->intron3TxOffset;
    swapUint(&txPos->txOffset, &txPos->intron3TxOffset);
    swapUint(&txPos->gDistance, &txPos->intron3Distance);
    }
// No change to aliBlkIx -- no change to alignment, it is still genomic + strand.
}

static void vpTxPosIntronic(struct vpTxPosition *txPos, struct psl *txAli, uint gOffset, int ix)
/* For introns [could there ever be double-sided gaps??] we need tx offsets and distances
 * for both 5' exon (before the intron) and 3' exon (after the intron).  */
{
uint intronStart = txAli->tStarts[ix] + txAli->blockSizes[ix];
if (gOffset < intronStart)
    errAbort("vpTxIntronicPos: gOffset (%u) < start of intron %d (%u)", gOffset, ix, intronStart);
uint intronEnd = txAli->tStarts[ix+1];
if (gOffset > intronEnd)
    errAbort("vpTxIntronicPos: gOffset (%u) > end of intron %d (%u)", gOffset, ix, intronEnd);
uint txLeft = txAli->qStarts[ix] + txAli->blockSizes[ix];
uint txRight = txAli->qStarts[ix+1];
txPos->txOffset = txLeft;
txPos->gDistance = gOffset - intronStart;
txPos->intron3TxOffset = txRight;
txPos->intron3Distance = intronEnd - gOffset;
}

static boolean genomeHasDeletion(struct psl *txAli, int ix)
/* Return TRUE if block ix has a gap to the right that skips 0 bases on target (reference genome)
 * and >0 bases on query (transcript) -- i.e. not an intron, but rather some missing base(s)
 * in the reference genome. */
{
if (ix < 0 || ix >= txAli->blockCount - 1)
    return FALSE;
int blockSize = txAli->blockSizes[ix];
return (txAli->tStarts[ix+1] == txAli->tStarts[ix] + blockSize &&
        txAli->qStarts[ix+1] > txAli->qStarts[ix] + blockSize);

}

// Google search on "minimal intron size" turned up a study of shortest known introns, ~48-50 in
// most species surveyed at the time.
#define MIN_INTRON 45

static boolean pslIntronTooShort(struct psl *psl, int blkIx, int minIntronSize)
/* Return TRUE if the target gap between blkIx and blkIx+1 is too short to be a plausible intron. */
{
if (blkIx >= psl->blockCount - 1 || blkIx < 0)
    errAbort("pslIntronTooShort: blkIx %d is out of range [0, %d]", blkIx, psl->blockCount - 1);
int intronLen = psl->tStarts[blkIx+1] - (psl->tStarts[blkIx] + psl->blockSizes[blkIx]);
return (intronLen < minIntronSize);
}

void vpPosGenoToTx(uint gOffset, struct psl *txAli, struct vpTxPosition *txPos, boolean isTxEnd)
/* Use txAli to project gOffset onto transcript-relative coords in txPos.
 * Set isTxEnd to TRUE if we are projecting to the end coordinate in transcript space:
 * higher genomic coord if transcript on '+' strand, lower genomic coord if tx on '-' strand. */
{
ZeroVar(txPos);
txPos->gOffset = gOffset;
boolean isRc = (pslQStrand(txAli) == '-');
// Coordinate transforms of start and end coordinates can be done the same way, but
// when determining which region of the transcript the variant falls in, we need to
// treat the open end differently (looking backward) from the closed start (looking forward).
int endCmp = (isTxEnd != isRc) ? 1 : 0;
int gOffsetCmp = gOffset - endCmp;
if (gOffsetCmp < txAli->tStart)
    {
    txPos->region = vpUpstream;
    // Can't use qStart here -- when isRc, qStarts are reversed but qStart and qEnd are not
    txPos->txOffset = txAli->qStarts[0];
    txPos->gDistance = txAli->tStart - gOffset;
    txPos->aliBlkIx = 0;
    }
else if (gOffsetCmp < txAli->tEnd)
    {
    int ix;
    for (ix = 0;  ix < txAli->blockCount;  ix++)
        {
        int tBlkStart = txAli->tStarts[ix];
        int tBlkEnd = tBlkStart + txAli->blockSizes[ix];
        if (!endCmp && ix > 0 && gOffset == tBlkStart && genomeHasDeletion(txAli, ix-1))
            {
            // Include adjacent skipped transcript bases to the left
            txPos->region = vpExon;
            txPos->txOffset = txAli->qStarts[ix-1] + txAli->blockSizes[ix-1];
            txPos->aliBlkIx = ix-1;
            break;
            }
        else if (endCmp && gOffset == tBlkEnd && genomeHasDeletion(txAli, ix))
            {
            // Include adjacent skipped transcript bases to the right
            txPos->region = vpExon;
            txPos->txOffset = txAli->qStarts[ix+1];
            txPos->aliBlkIx = ix+1;
            break;
            }
        else if (gOffsetCmp >= tBlkStart && gOffsetCmp < tBlkEnd)
            {
            // Normal exonic base
            txPos->region = vpExon;
            txPos->txOffset = txAli->qStarts[ix] + gOffset - tBlkStart;
            txPos->aliBlkIx = ix;
            break;
            }
        else if (ix < txAli->blockCount-1 &&
                 gOffsetCmp >= tBlkEnd && gOffsetCmp < txAli->tStarts[ix+1])
            {
            // Intronic -- or genomic insertion
            if (pslIntronTooShort(txAli, ix, MIN_INTRON))
                {
                // Genome has extra bases relative to transcript -- use tx exon position
                txPos->region = vpExon;
                if (endCmp)
                    {
                    txPos->txOffset = txAli->qStarts[ix+1];
                    txPos->gInsBases = txAli->tStarts[ix+1] - gOffset;
                    }
                else
                    {
                    txPos->txOffset = txAli->qStarts[ix] + txAli->blockSizes[ix];
                    txPos->gInsBases = gOffset - tBlkEnd;
                    }
                }
            else
                {
                txPos->region = vpIntron;
                vpTxPosIntronic(txPos, txAli, gOffset, ix);
                }
            txPos->aliBlkIx = ix;
            break;
            }
        }
    }
else
    {
    txPos->region = vpDownstream;
    // Can't use qEnd here -- when isRc, qStarts are reversed but qStart and qEnd are not
    int lastIx = txAli->blockCount - 1;
    txPos->txOffset = txAli->qStarts[lastIx] + txAli->blockSizes[lastIx];
    txPos->gDistance = gOffset - txAli->tEnd;
    txPos->aliBlkIx = lastIx;
    }
if (isRc)
    vpTxPosRc(txPos, txAli->qSize);
}

static char *getTxInRange(struct dnaSeq *txSeq, struct vpTxPosition *startPos,
                          struct vpTxPosition *endPos)
/* If [startPos, endPos) overlaps the actual transcript (i.e. is not intronic or up/down,
 * return the transcript sequence in that range (possibly empty).  If there is no overlap
 * then return NULL. */
 {
char *txRef = NULL;
if (endPos->txOffset >= startPos->txOffset)
    {
    int txRefLen = endPos->txOffset - startPos->txOffset;
    txRef = cloneStringZ(txSeq->dna + startPos->txOffset, txRefLen);
    }
if (txRef)
    touppers(txRef);
return txRef;
}

static void vpTxSetRef(struct vpTx *vpTx, struct dnaSeq *txSeq)
/* Set vpTx->txRef to transcript variant reference sequence from txSeq using vpTx start & end,
 * or NULL if variant has no overlap with transcript exons. */
{
freez(&vpTx->txRef);
vpTx->txRef = getTxInRange(txSeq, &vpTx->start, &vpTx->end);
}

static int pslCountExons(struct psl *psl, int minIntronSize)
/* Return the number of exons separated by gaps whose length on target is at least minIntronSize. */
{
int exonCount = 1;
int ix;
for (ix = 0;  ix < psl->blockCount-1;  ix++)
    {
    if (!pslIntronTooShort(psl, ix, minIntronSize))
        exonCount++;
    }
return exonCount;
}

static uint pslBlkIxToExonIx(struct psl *psl, int blkIx, int minIntronSize)
/* Convert PSL block to exon number, glossing over too-short introns */
{
uint exonIx = 0, ix;
if (blkIx < 0 || blkIx >= psl->blockCount)
    errAbort("pslBlkIxToExonIx: illegal blkIx %d (expecting [0..%d]", blkIx, psl->blockCount-1);
// Increment exonIx every time we pass an intron that's not too short.
if (pslQStrand(psl) == '-')
    for (ix = psl->blockCount - 1; ix > blkIx && ix > 0;  ix--)
        {
        if (!pslIntronTooShort(psl, ix-1, minIntronSize))
            exonIx++;
        }
else
    for (ix = 0;  ix < blkIx;  ix++)
        {
        if (! pslIntronTooShort(psl, ix, minIntronSize))
            exonIx++;
        }
return exonIx;
}

static size_t bufSizeForSplicedPlusGInsBases(struct psl *txAli, uint gStart, uint gEnd)
// Calculate the buffer size required for spliced genomic sequence plus sequence of
// suspiciously short "introns".
{
size_t bufSize = 0;
int ix;
for (ix = 0;  ix < txAli->blockCount;  ix++)
    {
    int tBlkStart = txAli->tStarts[ix];
    if (gEnd <= tBlkStart)
        break;
    int tBlkEnd = tBlkStart + txAli->blockSizes[ix];
    if (gStart < tBlkEnd)
        {
        int startInBlk = max(tBlkStart, gStart);
        int endInBlk = min(tBlkEnd, gEnd);
        int len = endInBlk - startInBlk;
        if (len > 0)
            bufSize += len;
        }
    int tNextBlkStart = txAli->tStarts[ix+1];
    if (ix < txAli->blockCount - 1 && gEnd >= tBlkEnd && gStart <= tNextBlkStart &&
        pslIntronTooShort(txAli, ix, MIN_INTRON))
        {
        // It's an indel between genome and transcript, not an intron -- add genomic sequence
        int len = tNextBlkStart - tBlkEnd;
        if (len > 0)
            bufSize += len;
        }
    }
bufSize++;
return bufSize;
}

static void appendOverlap(struct seqWindow *gSeqWin, uint blkStart, uint blkEnd,
                          uint gStart, uint gEnd, char *buf, size_t bufSize, int *pBufOffset)
/* If there is any overlap between [blkStart,blkEnd) and [gStart,gEnd) then append the genomic
 * sequence to buf and update *pBufOffset. */
{
int startInBlk = max(blkStart, gStart);
int endInBlk = min(blkEnd, gEnd);
int len = endInBlk - startInBlk;
if (len > 0)
    {
    assert(bufSize > *pBufOffset + len);
    seqWindowCopy(gSeqWin, startInBlk, len, buf+*pBufOffset, bufSize-*pBufOffset);
    *pBufOffset += len;
    }
}

static void spliceGenomicInRange(struct seqWindow *gSeqWin, uint gStart, uint gEnd,
                                 struct psl *txAli, boolean checkIntrons, char *buf, size_t bufSize)
/* Splice genomic exons in range into buf and reverse-complement if necessary.
 * If checkIntrons is true then include genomic sequence from "introns" that are too short to be
 * actual introns; use bufSizeForSplicedPlusGInsBases to calc bufSize before calling this. */
{
int splicedLen = 0;
buf[0] = 0;
int ix;
for (ix = 0;  ix < txAli->blockCount;  ix++)
    {
    int tBlkStart = txAli->tStarts[ix];
    if (tBlkStart >= gEnd)
        break;
    int tBlkEnd = tBlkStart + txAli->blockSizes[ix];
    appendOverlap(gSeqWin, tBlkStart, tBlkEnd, gStart, gEnd, buf, bufSize, &splicedLen);
    int tNextBlkStart = txAli->tStarts[ix+1];
    if (checkIntrons && ix < txAli->blockCount - 1 && gEnd >= tBlkEnd && gStart <= tNextBlkStart &&
        pslIntronTooShort(txAli, ix, MIN_INTRON))
        {
        // It's an indel between genome and transcript, not an intron -- add genomic sequence
        int len = tNextBlkStart - tBlkEnd;
        if (len > 0)
            {
            seqWindowCopy(gSeqWin, tBlkEnd, len, buf+splicedLen, bufSize-splicedLen);
            splicedLen += len;
            }
        }
    }
boolean isRc = (pslQStrand(txAli) == '-');
if (isRc && splicedLen)
    reverseComplement(buf, splicedLen);
}

static boolean genomeTxMismatch(char *txRef, struct seqWindow *gSeqWin,
                                uint gStart, uint gEnd, struct psl *txAli)
/* If the variant overlaps aligned blocks then compare spliced strand-corrected genomic reference
 * sequence with transcript reference sequence.  vpTx start, end and txRef must be in place.
 * Note: this will detect substitutions but not indels by design -- leave it to processIndels
 * and vpTxSetTxAlt to detect indel mismatches. */
{
boolean mismatch = FALSE;
if (txRef != NULL)
    {
    int bufLen = gEnd - gStart + 1;
    char splicedGSeq[bufLen];
    splicedGSeq[0] = '\0';
    spliceGenomicInRange(gSeqWin, gStart, gEnd, txAli, FALSE, splicedGSeq, sizeof(splicedGSeq));
    if (differentString(splicedGSeq, txRef))
        mismatch = TRUE;
    }
return mismatch;
}

boolean vpTxPosIsInsertion(struct vpTxPosition *startPos, struct vpTxPosition *endPos)
/* Return TRUE if startPos and endPos describe a zero-length region. */
{
// Sometimes an insertion happens at the boundary between regions, in which case startPos->region
// and endPos->region will be different even though they land on the same point.
// At the boundary, endPos->region is looking left/5' and startPos->region is looking right/3'.
return (startPos->txOffset == endPos->txOffset &&
        ((startPos->gDistance == endPos->gDistance &&
          startPos->intron3TxOffset == endPos->intron3TxOffset &&
          startPos->intron3Distance == endPos->intron3Distance) ||
         // intron -> exon boundary: startPos is exonic (right), endPos is intronic (left),
         // but end->intron3Distance is 0
         (startPos->region == vpExon && endPos->region == vpIntron &&
          endPos->intron3Distance == 0) ||
         // exon -> intron boundary: startPos is intronic (right), endPos is exonic (left),
         // but start->gDistance is 0
         (startPos->region == vpIntron && endPos->region == vpExon &&
          startPos->gDistance == 0)));
}

static int limitToExon(struct vpTx *vpTx, uint gEnd, struct psl *txAli)
/* If variant ends in an exon, return the max number of bases by which we can shift the variant
 * along the genome in the direction of transcription without running past the end of the exon
 * into a splice site.
 * See HGVS "exception 3' rule": http://varnomen.hgvs.org/bg-material/numbering/
 * If not applicable, return INDEL_SHIFT_NO_MAX. */
{
int maxShift = INDEL_SHIFT_NO_MAX;
if (vpTx->end.region == vpExon)
    {
    int blkIx = vpTx->end.aliBlkIx;
    if (txAli->strand[0] == '-')
        {
        while (blkIx > 0 && pslIntronTooShort(txAli, blkIx-1, MIN_INTRON))
            blkIx--;
        int tBlkStart = txAli->tStarts[blkIx];
        maxShift = gEnd - tBlkStart;
        }
    else
        {
        while (blkIx < txAli->blockCount - 1 && pslIntronTooShort(txAli, blkIx, MIN_INTRON))
            blkIx++;
        int tBlkEnd = txAli->tStarts[blkIx] + txAli->blockSizes[blkIx];
        maxShift = tBlkEnd - gEnd;
        }
    }
return maxShift;
}

static boolean hasAnomalousGaps(struct psl *txAli, uint gStart, uint gEnd)
/* Return TRUE if txAli has an indel between genomic start & end that is too short to be intron. */
{
int ix;
for (ix = 0;  ix < txAli->blockCount - 1;  ix++)
    {
    uint intronStart = txAli->tStarts[ix] + txAli->blockSizes[ix];
    uint intronEnd = txAli->tStarts[ix+1];
    if (gStart <= intronEnd && gEnd >= intronStart && pslIntronTooShort(txAli, ix, MIN_INTRON))
        return TRUE;
    else if (intronStart > gEnd)
        break;
    }
return FALSE;
}

static char *cloneMaybeRc(char *seq, boolean isRc)
/* Clone a sequence string; if isRc, reverseComplement it. */
{
int len = strlen(seq);
char *copy = cloneStringZ(seq, len);
if (isRc)
    reverseComplement(copy, len);
return copy;
}

static void vpTxSetTxAlt(struct vpTx *vpTx, struct seqWindow *gSeqWin, uint gStart, uint gEnd,
                         char *gAlt, boolean isRc)
/* Set vpTx->txAlt to strand-corrected gAlt plus adjacent genomic insertion bases if any. */
{
if (vpTx->start.gInsBases || vpTx->end.gInsBases)
    {
    // variant overlaps genomic insertion; add genomic inserted bases to txAlt
    uint gibLeft = isRc ? vpTx->end.gInsBases : vpTx->start.gInsBases;
    uint gibRight = isRc ? vpTx->start.gInsBases : vpTx->end.gInsBases;
    uint altLen = strlen(gAlt);
    uint txAltLen = gibLeft + altLen + gibRight;
    char txAlt[txAltLen+1];
    if (gibLeft)
        seqWindowCopy(gSeqWin, gStart - gibLeft, gibLeft, txAlt, sizeof(txAlt));
    safecpy(txAlt+gibLeft, sizeof(txAlt)-gibLeft, gAlt);
    if (gibRight)
        seqWindowCopy(gSeqWin, gEnd, gibRight, txAlt+gibLeft+altLen, sizeof(txAlt)-gibLeft-altLen);
    if (isRc)
        reverseComplement(txAlt, txAltLen);
    vpTx->txAlt = cloneString(txAlt);
    vpTx->genomeMismatch = TRUE;
    int placeholder = 0;
    trimRefAlt(vpTx->txRef, vpTx->txAlt, &vpTx->start.txOffset, &vpTx->end.txOffset,
               &placeholder, &placeholder);
    }
else
    vpTx->txAlt = cloneMaybeRc(gAlt, isRc);
}

static void spliceInBlk(struct seqWindow *gSeqWin, uint blkStart, uint blkEnd,
                        uint ambigStart, uint ambigEnd, uint varStart, uint varEnd, char *gAlt,
                        char *txAlt, size_t txAltSize, int *pSplicedLen, boolean *pAddedGAlt)
/* If there is any overlap between [blkStart,blkEnd) and [ambigStart,ambigEnd) then append the
 * sequence to txAlt and update *pSplicedLen -- except for [varStart,varEnd) where we add
 * gAlt instead and update *pAddedGalt. */
{
if ((ambigStart < blkEnd && ambigEnd > blkStart) ||
    // both var and ambig are zero-length insertion points
    (ambigStart == ambigEnd && ambigStart <= blkEnd && ambigEnd >= blkStart))
    {
    // Add in blk's sequence between ambigStart and varStart, if any
    appendOverlap(gSeqWin, blkStart, blkEnd, ambigStart, varStart, txAlt, txAltSize, pSplicedLen);
    // If gAlt hasn't already been added, and blk overlaps [varStart,varEnd), add gAlt
    if (varStart >= blkStart && varEnd <= blkEnd && !*pAddedGAlt)
        {
        safecpy(txAlt+*pSplicedLen, txAltSize-*pSplicedLen, gAlt);
        *pSplicedLen += strlen(gAlt);
        *pAddedGAlt = TRUE;
        }
    // Add in blk's sequence between varEnd and ambigEnd, if any
    appendOverlap(gSeqWin, blkStart, blkEnd, varEnd, ambigEnd, txAlt, txAltSize, pSplicedLen);
    }
}

static char *spliceGenomicAndAlt(struct seqWindow *gSeqWin, uint ambigStart, uint ambigEnd,
                                 uint varStart, uint varEnd, struct psl *txAli, char *gAlt)
/* Carefully construct the modified transcribed sequence by retaining non-intron genomic inserted
 * sequence and splicing in gAlt (which is on + strand of genome) in place of varStart..varEnd. */
{
assert(ambigStart <= varStart);
assert(varStart <= varEnd);
assert(varEnd <= ambigEnd);
// This may count genomic inserted bases twice, but that's OK when allocating mem:
uint leftLen = bufSizeForSplicedPlusGInsBases(txAli, ambigStart, varStart) - 1;
uint rightLen = bufSizeForSplicedPlusGInsBases(txAli, varEnd, ambigEnd) - 1;
uint gAltLen = strlen(gAlt);
uint txAltSize = leftLen + gAltLen + rightLen + 1;
char *txAlt = needMem(txAltSize);
boolean addedGAlt = FALSE;
int splicedLen = 0;
int ix;
for (ix = 0;  ix < txAli->blockCount;  ix++)
    {
    int tBlkStart = txAli->tStarts[ix];
    if (tBlkStart >= ambigEnd)
        break;
    int tBlkEnd = tBlkStart + txAli->blockSizes[ix];
    spliceInBlk(gSeqWin, tBlkStart, tBlkEnd, ambigStart, ambigEnd,
                varStart, varEnd, gAlt, txAlt, txAltSize, &splicedLen, &addedGAlt);
    int tNextBlkStart = txAli->tStarts[ix+1];
    if (ix < txAli->blockCount - 1 && ambigEnd >= tBlkEnd && ambigStart <= tNextBlkStart &&
        pslIntronTooShort(txAli, ix, MIN_INTRON))
        {
        // Genomic insertion not intron -- splice in *all* inserted bases (except var bases).
        spliceInBlk(gSeqWin, tBlkEnd, tNextBlkStart,
                    min(tBlkEnd, ambigStart), max(ambigEnd, tNextBlkStart),
                    varStart, varEnd, gAlt, txAlt, txAltSize, &splicedLen, &addedGAlt);
        }
    }
boolean isRc = (pslQStrand(txAli) == '-');
if (isRc && splicedLen)
    reverseComplement(txAlt, splicedLen);
return txAlt;
}

static void processIndels(struct vpTx *vpTx, struct seqWindow *gSeqWin,
                          uint gStart, uint gEnd, char *gAltCpy,
                          struct psl *txAli, struct dnaSeq *txSeq)
/* If variant is an insertion or deletion, detect whether its placement is ambiguous, i.e. could
 * be shifted left or right with the same result.  Also detect whether txAli happens to have
 * a non-intronic indel in the same ambiguous region -- in that case, the genomic variant might
 * actually mean no change to the transcript so compare carefully.  If txAli is on '-' strand,
 * gTxStart can be greater than gTxEnd (i.e. they are swapped relative to the genome). */
{
int refLen = gEnd - gStart, altLen = strlen(gAltCpy);
boolean isRc = (pslQStrand(txAli) == '-');
if (! vpTx->genomeMismatch && indelShiftIsApplicable(refLen, altLen))
    {
    boolean isRc = (pslQStrand(txAli) == '-');
    // Genomic coords for transcript start and transcript end -- swapped if isRc!
    uint gTxStart = gStart, gTxEnd = gEnd;
    if (isRc)
        swapUint(&gTxStart, &gTxEnd);
    // Attempt to shift variants along the genome as far in the direction of transcription
    // as possible -- but not past an exon's 3' boundary into a splice site / intron.
    // shifting: http://andrewjesaitis.com/2017/03/the-state-of-variant-annotation-in-2017/
    // HGVS "exception 3' rule": http://varnomen.hgvs.org/bg-material/numbering/
    // Also find out how far we could shift 5' so we can detect genome/tx non-intron indels.
    uint gTxStart5 = gTxStart, gTxEnd5 = gTxEnd;
    char gAltCpy5[altLen+1];
    safecpy(gAltCpy5, sizeof(gAltCpy5), gAltCpy);
    uint ambigStart, ambigEnd;
    if (isRc)
        {
        indelShift(gSeqWin, &gTxEnd5, &gTxStart5, gAltCpy5, INDEL_SHIFT_NO_MAX, isdRight);
        int maxShift = limitToExon(vpTx, gTxEnd5, txAli);
        vpTx->basesShifted = indelShift(gSeqWin, &gTxEnd, &gTxStart, gAltCpy, maxShift, isdLeft);
        ambigStart = gTxEnd;
        ambigEnd = gTxStart5;
        }
    else
        {
        indelShift(gSeqWin, &gTxStart5, &gTxEnd5, gAltCpy5, INDEL_SHIFT_NO_MAX, isdLeft);
        int maxShift = limitToExon(vpTx, gTxEnd5, txAli);
        vpTx->basesShifted = indelShift(gSeqWin, &gTxStart, &gTxEnd, gAltCpy, maxShift, isdRight);
        ambigStart = gTxStart5;
        ambigEnd = gTxEnd;
        }
    if (vpTx->basesShifted)
        {
        // Variant was shifted on genome; re-project genomic coordinates to tx.
        vpPosGenoToTx(gTxStart, txAli, &vpTx->start, FALSE);
        vpPosGenoToTx(gTxEnd, txAli, &vpTx->end, TRUE);
        vpTxSetRef(vpTx, txSeq);
        }
    // Now that that's settled, set gRef and gAlt:
    gStart = isRc ? gTxEnd : gTxStart;
    gEnd = isRc ? gTxStart : gTxEnd;
    vpTx->gRef = needMem(refLen+1);
    seqWindowCopy(gSeqWin, gStart, refLen, vpTx->gRef, refLen+1);
    if (isRc)
        reverseComplement(vpTx->gRef, refLen);
    vpTx->gAlt = cloneMaybeRc(gAltCpy, isRc);
    vpTxSetTxAlt(vpTx, gSeqWin, gStart, gEnd, gAltCpy, isRc);
    if (hasAnomalousGaps(txAli, ambigStart, ambigEnd))
        {
        // The transcript and genome have a non-intron indel in this indel region,
        // so this variant could actually mean no change to the transcript.  We need to
        // carefully compare the transcript sequence to the genomic alt sequence over the
        // whole region.
        struct vpTxPosition ambigStartPos, ambigEndPos;
        vpPosGenoToTx(isRc ? ambigEnd : ambigStart, txAli, &ambigStartPos, FALSE);
        vpPosGenoToTx(isRc ? ambigStart : ambigEnd, txAli, &ambigEndPos, TRUE);
        char *ambigTxRef = getTxInRange(txSeq, &ambigStartPos, &ambigEndPos);
        // First get the reference according to the genome -- retaining genomic sequence from
        // the non-intron indel(s).
        size_t bufSize = bufSizeForSplicedPlusGInsBases(txAli, ambigStart, ambigEnd);
        char ambigGRef[bufSize];
        spliceGenomicInRange(gSeqWin, ambigStart, ambigEnd, txAli, TRUE,
                             ambigGRef, sizeof(ambigGRef));
        //#*** What if ambigStart.region != ambigEnd.region?
        if (differentString(ambigTxRef, ambigGRef))
            {
            // The ambiguous region contains a non-intron indel... construct ambiguous alt:
            char *ambigAlt = spliceGenomicAndAlt(gSeqWin, ambigStart, ambigEnd, gStart, gEnd, txAli,
                                                 gAltCpy);
            freeMem(vpTx->txAlt);
            if (sameString(ambigTxRef, ambigAlt))
                {
                // No change to the transcript; keep the right-shifted tx coords so that a
                // would-have-been substitution doesn't become a confusing insertion point that
                // makes it seem like the base to the right was somehow involved.
                vpTx->txAlt = cloneString(vpTx->txRef);
                }
            else
                {
                // There was some change to the transcript; trim redundant bases from the
                // ambiguous region ref and alt to get the right-shifted change.
                int placeholder = 0;
                //#*** will vpTx->start/end.region ever change from this??
                trimRefAlt(ambigTxRef, ambigAlt, &ambigStartPos.txOffset, &ambigEndPos.txOffset,
                           &placeholder, &placeholder);
                vpTx->start.txOffset = ambigStartPos.txOffset;
                vpTx->end.txOffset = ambigEndPos.txOffset;
                freeMem(vpTx->txRef);
                vpTx->txRef = ambigTxRef;
                vpTx->txAlt = ambigAlt;
                }
            vpTx->genomeMismatch = TRUE;
            }
        }
    }
else if (hasAnomalousGaps(txAli, gStart, gEnd))
    {
    vpTx->gRef = needMem(refLen+1);
    seqWindowCopy(gSeqWin, gStart, refLen, vpTx->gRef, refLen+1);
    if (isRc)
        reverseComplement(vpTx->gRef, refLen);
    vpTx->gAlt = cloneMaybeRc(gAltCpy, isRc);
    if (differentString(vpTx->txRef, vpTx->gRef))
        {
        vpTx->txAlt = spliceGenomicAndAlt(gSeqWin, gStart, gEnd, gStart, gEnd, txAli, gAltCpy);
        if (differentString(vpTx->txRef, vpTx->txAlt))
            {
            // There was some change to the transcript; trim redundant bases (if any) from tx
            // ref and alt.
            int placeholder = 0;
            trimRefAlt(vpTx->txRef, vpTx->txAlt, &vpTx->start.txOffset, &vpTx->end.txOffset,
                       &placeholder, &placeholder);
            }
        vpTx->genomeMismatch = TRUE;
        }
    }
}

struct vpTx *vpGenomicToTranscript(struct seqWindow *gSeqWin, struct bed3 *gBed3, char *gAlt,
                                   struct psl *txAli, struct dnaSeq *txSeq)
/* Project a genomic variant onto a transcript, trimming identical bases at the beginning and/or
 * end of ref and alt alleles and shifting ambiguous indel placements in the direction of
 * transcription except across an exon-intron boundary.
 * Both ref and alt must be [ACGTN]-only (no symbolic alleles like "." or "-" or "<DEL>").
 * This may change gSeqWin's range. */
{
if (sameString(gAlt, "<DEL>"))
    gAlt[0] = '\0';
int altLen = strlen(gAlt);
if (!isAllNt(gAlt, altLen
             +1)) //#*** FIXME isAllNt ignores last base in string!!! always TRUE for len=1
    errAbort("vpGenomicToTranscript: alternate allele must be sequence of IUPAC DNA characters "
             "but is '%s'", gAlt);
gSeqWin->fetch(gSeqWin, gBed3->chrom, gBed3->chromStart, gBed3->chromEnd);
struct vpTx *vpTx;
AllocVar(vpTx);
vpTx->txName = cloneString(txSeq->name);
boolean isRc = (pslQStrand(txAli) == '-');
// Genomic coords and sequence for variant -- trim identical bases from ref and alt if any:
uint gStart = gBed3->chromStart, gEnd = gBed3->chromEnd;
int refLen = gEnd - gStart;
char gRef[refLen+1];
seqWindowCopy(gSeqWin, gStart, refLen, gRef, sizeof(gRef));
char gAltCpy[altLen+1];
safecpy(gAltCpy, sizeof(gAltCpy), gAlt);
if (differentString(gRef, gAltCpy))
    // Trim identical bases from start and end -- unless this is an assertion that there is
    // no change, in which case it's good to keep the range on which that assertion was made.
    trimRefAlt(gRef, gAltCpy, &gStart, &gEnd, &refLen, &altLen);
// Even if we may later shift the variant position in the direction of transcription, first do
// an initial projection to find exon boundaries and detect mismatch between genome and tx.
vpPosGenoToTx(isRc ? gEnd : gStart, txAli, &vpTx->start, FALSE);
vpPosGenoToTx(isRc ? gStart : gEnd, txAli, &vpTx->end, TRUE);
// Compare genomic ref allele vs. txRef
vpTxSetRef(vpTx, txSeq);
vpTx->genomeMismatch = genomeTxMismatch(vpTx->txRef, gSeqWin, gStart, gEnd, txAli);
processIndels(vpTx, gSeqWin, gStart, gEnd, gAltCpy, txAli, txSeq);
// processIndels may or may not set vpTx->gAlt, vpTx->txAlt and vpTx->gRef
if (vpTx->gAlt == NULL)
    vpTx->gAlt = cloneMaybeRc(gAltCpy, isRc);
if (vpTx->txAlt == NULL)
    vpTxSetTxAlt(vpTx, gSeqWin, gStart, gEnd, gAltCpy, isRc);
if (vpTx->gRef == NULL)
    vpTx->gRef = cloneMaybeRc(gRef, isRc);
return vpTx;
}

void vpTxFree(struct vpTx **pVpTx)
/* Free up a vpTx. */
{
if (pVpTx)
    {
    struct vpTx *vpTx = *pVpTx;
    freeMem(vpTx->txName);
    freeMem(vpTx->txRef);
    freeMem(vpTx->txAlt);
    freeMem(vpTx->gRef);
    freeMem(vpTx->gAlt);
    freez(pVpTx);
    }
}

char *translateString(char *codons)
/* Translate a string of codon DNA into a string of peptide bases.  stop codon is 'X'. */
{
struct dnaSeq *codonSeq = newDnaSeq(cloneString(codons), strlen(codons), NULL);
aaSeq *alt = translateSeq(codonSeq, 0, FALSE);
aaSeqZToX(alt);
dnaSeqFree(&codonSeq);
return dnaSeqCannibalize(&alt);
}

struct vpPep *vpTranscriptToProtein(struct vpTx *vpTx, struct genbankCds *cds,
                                    struct dnaSeq *txSeq, struct dnaSeq *protSeq)
/* Project a coding transcript variant onto a protein sequence, shifting position to the first
 * differing amino acid position.  Return NULL if no cds or incomplete cds. */
//#*** This will produce incorrect results for the rare cds with join(...) unless we make a more
//#*** complicated cds data structure to represent those (basically list of cds's) and use it here.
{
if (cds == NULL || cds->start == -1 || cds->end == -1 || !cds->startComplete)
    return NULL;
if (txSeq == NULL)
    errAbort("vpTranscriptToProtein: txSeq must not be NULL");
struct vpPep *vpPep = NULL;
AllocVar(vpPep);
vpPep->name = cloneString(protSeq->name);
uint txStart = vpTx->start.txOffset;
uint txEnd = vpTx->end.txOffset;
// If the variant starts and ends within exon(s) and overlaps CDS then predict protein change.
if (txStart >= cds->start && txStart < cds->end && txEnd > cds->start &&
    ((vpTx->start.region == vpExon && vpTx->end.region == vpExon) ||
     // Insertion at exon boundary -- it doesn't disrupt the splice site so assume its effect
     // is on the exon, in the spirit of HGVS's 3' exception rule
     (vpTxPosIsInsertion(&vpTx->start, &vpTx->end) &&
      (vpTx->start.region == vpExon || vpTx->end.region == vpExon))))
    {
    uint startInCds = max(txStart, cds->start) - cds->start;
    uint endInCds = min(txEnd, cds->end) - cds->start;
    vpPep->start = startInCds / 3;
    vpPep->end = (endInCds + 2) / 3;
    uint codonStartInCds = vpPep->start * 3;
    uint codonEndInCds = vpPep->end * 3;
    uint codonLenInCds = codonEndInCds - codonStartInCds;
    aaSeq *txTrans = translateSeqN(txSeq, cds->start + codonStartInCds,
                                   codonLenInCds, FALSE);
    aaSeqZToX(txTrans);
    // We need pSeq to end with "X" because vpPep->start can be the stop codon / terminal
    char *pSeq = protSeq->dna;
    int pLen = protSeq->size;
    char pSeqWithX[pLen+2];
    if (pSeq[pLen-1] != 'X')
        {
        safencpy(pSeqWithX, sizeof(pSeqWithX), pSeq, pLen);
        safencpy(pSeqWithX+pLen, sizeof(pSeqWithX)-pLen, "X", 1);
        pSeq = pSeqWithX;
        }
    vpPep->txMismatch = !sameStringN(txTrans->dna, pSeq+vpPep->start,
                                     vpPep->end - vpPep->start);
    int startPadding = (startInCds - codonStartInCds);
    int endPadding = codonEndInCds - endInCds;
    int txAltLen = strlen(vpTx->txAlt);
    char altCodons[txSeq->size+txAltLen+1];
    altCodons[0] = '\0';
    if (startPadding > 0)
        // Copy the unchanged first base or two of ref codons
        safencpy(altCodons, sizeof(altCodons),
                 txSeq->dna + cds->start + codonStartInCds, startPadding);
    int txRefLen = txEnd - txStart;
    uint utr5Bases = (cds->start > txStart) ? cds->start - txStart : 0;
    if (txAltLen > utr5Bases)
        // Copy in the alternate allele
        safencpy(altCodons+startPadding, sizeof(altCodons)-startPadding,
                 vpTx->txAlt + utr5Bases, txAltLen - utr5Bases);
    if ((txRefLen - txAltLen) % 3 != 0)
        {
        vpPep->frameshift = TRUE;
        // Extend ref to the end of the protein.
        vpPep->ref = cloneString(pSeq+vpPep->start);
        // Copy in all remaining tx sequence to see how soon we would hit a stop codon
        safecpy(altCodons+startPadding+txAltLen, sizeof(altCodons)-startPadding-txAltLen,
                txSeq->dna + txEnd);
        }
    else
        {
        vpPep->ref = cloneStringZ(pSeq+vpPep->start, vpPep->end - vpPep->start);
        if (endPadding > 0)
            // Copy the unchanged last base or two of ref codons
            safencpy(altCodons+startPadding+txAltLen,
                     sizeof(altCodons)-startPadding-txAltLen,
                     txSeq->dna + cds->start + endInCds, endPadding);
        }
    char *alt = translateString(altCodons);
    if (endsWith(vpPep->ref, "X") && !endsWith(alt, "X"))
        {
        // Stop loss -- recompute alt
        freeMem(alt);
        safecpy(altCodons+startPadding+txAltLen, sizeof(altCodons)-startPadding-txAltLen,
                txSeq->dna + txEnd);
        alt = translateString(altCodons);
        }
    vpPep->alt = alt;
    int refLen = strlen(vpPep->ref), altLen = strlen(vpPep->alt);
    if (differentString(vpPep->ref, vpPep->alt))
        {
        // If alt has a stop codon, temporarily disguise it so it can't get trimmed
        char *altStop = strchr(vpPep->alt, 'X');
        if (altStop)
            *altStop = 'Z';
        trimRefAlt(vpPep->ref, vpPep->alt, &vpPep->start, &vpPep->end, &refLen, &altLen);
        if (altStop)
            *strchr(vpPep->alt, 'Z') = 'X';
        }
    if (indelShiftIsApplicable(refLen, altLen))
        {
        struct seqWindow *pSeqWin = memSeqWindowNew(vpPep->name, pSeq);
        vpPep->rightShiftedBases = indelShift(pSeqWin, &vpPep->start, &vpPep->end,
                                              vpPep->alt, INDEL_SHIFT_NO_MAX, isdRight);
        memSeqWindowFree(&pSeqWin);
        }
    dnaSeqFree((struct dnaSeq **)&txTrans);
    }
else
    {
    vpPep->cantPredict = TRUE;
    }
return vpPep;
}

void vpPepFree(struct vpPep **pVp)
/* Free up a vpPep. */
{
if (pVp && *pVp)
    {
    struct vpPep *vp = *pVp;
    freeMem(vp->name);
    freeMem(vp->ref);
    freeMem(vp->alt);
    freez(pVp);
    }
}

static char *regionStrings[] =
    { "vpUnknown",
      "vpUpstream",
      "vpDownstream",
      "vpExon",
      "vpIntron",
    };

char *vpTxRegionToString(enum vpTxRegion region)
/* Return a static string for region.  Do not free result! */
{
if (region > ArraySize(regionStrings))
    errAbort("vpTxRegionToString: invalid region %d", region);
return regionStrings[region];
}

void vpTxPosSlideInSameRegion(struct vpTxPosition *txPos, int bases)
/* Move txPos's region-appropriate offsets and distances by bases (can be negative).
 * *Caller must ensure that this will not slide us into another region!* */
{
if (txPos->region == vpIntron)
    {
    txPos->gDistance += bases;
    txPos->intron3Distance -= bases;
    }
else if (txPos->region == vpExon)
    txPos->txOffset += bases;
else if (txPos->region == vpUpstream)
    txPos->gDistance -= bases;
else if (txPos->region == vpDownstream)
    txPos->gDistance += bases;
else
    errAbort("vpTxPosSlideInSameRegion: unrecognized region %d", txPos->region);
}

boolean vpTxPosRangeIsSingleBase(struct vpTxPosition *startPos, struct vpTxPosition *endPos)
/* Return true if [startPos, endPos) is a single-base region. */
{
if (startPos->region != endPos->region)
    return FALSE;
if (startPos->region == vpUpstream && startPos->gDistance == endPos->gDistance + 1)
    return TRUE;
if (startPos->region == vpExon && startPos->txOffset + 1 == endPos->txOffset)
    return TRUE;
if (startPos->region == vpIntron &&
    startPos->txOffset == endPos->txOffset && startPos->gDistance + 1 == endPos->gDistance)
    return TRUE;
if (startPos->region == vpDownstream && startPos->gDistance + 1 == endPos->gDistance)
    return TRUE;
return FALSE;
}

char *vpTxGetRef(struct vpTx *vpTx)
/* If vpTx->txRef is non-NULL and both start & end are exonic, return txRef;
 * otherwise return genomic.  For example, if a deletion spans exon/intron boundary, use genomic
 * ref because it includes the intron bases.  Do not free the returned value. */
{
if (vpTx->txRef != NULL &&
    vpTx->start.region == vpExon && vpTx->end.region == vpExon)
    return vpTx->txRef;
return vpTx->gRef;
}

static char *getGAlt(struct vpTx *vpTx, struct psl *psl)
/* Return a possibly strand-swapped copy of vpTx->gAlt.  Do not free returned value. */
{
static struct dyString *dy = NULL;
if (dy == NULL)
    dy = dyStringNew(0);
dyStringClear(dy);
int altLen = strlen(vpTx->gAlt);
dyStringAppendN(dy, vpTx->gAlt, altLen);
if (pslQStrand(psl) == '-')
    reverseComplement(dy->string, altLen);
return dy->string;
}

static void getRefAltCodon(struct vpTx *vpTx, struct genbankCds *cds, struct dnaSeq *txSeq,
                           struct codingChange *cc, struct lm *lm)
/* Make an in-frame representation of modified CDS sequence. */
{
char *txCdsSeq = txSeq->dna + cds->start;
int cdsStart = vpTx->start.txOffset - cds->start;
int basesBefore = cdsStart % 3;
int codonStart = cdsStart - basesBefore;
int cdsEnd = vpTx->end.txOffset - cds->start;
int basesAfter = (3 - cdsEnd % 3) % 3;
int txAltLen = strlen(vpTx->txAlt);
if (basesBefore + (cdsEnd - cdsStart) + basesAfter == 0 &&
    txAltLen % 3 != 0)
    // Frameshift insertion at codon boundary -- show disrupted codon instead of just ""
    basesAfter = 3;
int refCodonLen = cdsEnd + basesAfter - codonStart;
cc->codonOld = lmCloneStringZ(lm, txCdsSeq + codonStart, refCodonLen);
size_t codonNewSize = basesBefore + txAltLen + basesAfter + 1;
char *codonNew = lmAlloc(lm, codonNewSize);
if (basesBefore > 0)
    safencpy(codonNew, codonNewSize, txCdsSeq + codonStart, basesBefore);
if (txAltLen > 0)
    safencpy(codonNew + basesBefore, codonNewSize, vpTx->txAlt, txAltLen);
if (basesAfter > 0)
    safencpy(codonNew + basesBefore + txAltLen, codonNewSize, txCdsSeq + cdsEnd, basesAfter);
cc->codonNew = codonNew;
}

static boolean pslNmdTarget(struct psl *psl, struct genbankCds *cds, int minIntronSize)
/* Use psl and cds to determine whether a transcript is already subject to
 * nonsense-mediated decay (NMD), i.e. cds end is more than 50bp upstream of last intron.
 * If minIntronSize > 0, treat gaps between psl blocks as true introns only if they are
 * at least that many bases long.  If pslQStrand(psl) is '-', this calls pslRc twice. */
{
if (psl->blockCount < 2 || cds == NULL || cds->start == cds->end)
    return FALSE;
int numIntronsPastCds = 0;
int cdsEndToIntron = 0;
boolean isRc = (pslQStrand(psl) == '-');
boolean noTStrand = (psl->strand[1] == '\0');
// Use pslRc so all math is on query + strand (cds->end is query +)
if (isRc)
    pslRc(psl);
int ix;
for (ix = 0;  ix < psl->blockCount-1;  ix++)
    {
    int exonQStart = psl->qStarts[ix];
    int exonQEnd = exonQStart + psl->blockSizes[ix];
    if (exonQEnd >= cds->end)
        {
        // t coords are now on the reverse strand and in reverse order; intron size works the same
        int exonTEnd = psl->tStarts[ix] + psl->blockSizes[ix];
        int nextExonTStart = psl->tStarts[ix+1];
        int intronSize = nextExonTStart - exonTEnd;
        if (intronSize > minIntronSize)
            {
            if (numIntronsPastCds == 0)
                {
                // First real intron following cdsEnd
                cdsEndToIntron = exonQEnd - cds->end;
                }
            numIntronsPastCds++;
            }
        }
    }
if (isRc)
    {
    pslRc(psl);
    if (noTStrand)
        psl->strand[1] = '\0';
    }
return (numIntronsPastCds > 1 || cdsEndToIntron > 50);
}

static void setCodingInfo(struct gpFx *fx, struct vpTx *vpTx, struct psl *psl,
                          struct genbankCds *cds, struct dnaSeq *txSeq,
                          struct vpPep *vpPep, struct lm *lm)
/* Fill in the values of fx->details.codingChange. */
{
struct codingChange *cc = &fx->details.codingChange;
cc->cDnaPosition = vpTx->start.txOffset;
cc->txRef = lmCloneString(lm, vpTx->txRef);
cc->txAlt = lmCloneString(lm, vpTx->txAlt);
cc->cdsPosition = vpTx->start.txOffset - cds->start;
cc->exonNumber = pslBlkIxToExonIx(psl, vpTx->start.aliBlkIx, MIN_INTRON);
cc->exonCount = pslCountExons(psl, MIN_INTRON);
getRefAltCodon(vpTx, cds, txSeq, cc, lm);
cc->pepPosition = vpPep->start;
uint pepRefLen = strlen(vpPep->ref);
uint pepAltLen = strlen(vpPep->alt);
cc->aaOld = lmCloneStringZ(lm, vpPep->ref, pepRefLen);
if (pepRefLen > 0 && cc->aaOld[pepRefLen-1] == 'X')
    cc->aaOld[pepRefLen-1] = '*';
cc->aaNew = lmCloneStringZ(lm, vpPep->alt, pepAltLen);
if (pepAltLen > 0 && cc->aaNew[pepAltLen-1] == 'X')
    cc->aaNew[pepAltLen-1] = '*';
}

static struct gpFx *vpTxToFxCds(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                                struct dnaSeq *txSeq, struct vpPep *vpPep,
                                struct dnaSeq *protSeq, struct lm *lm)
/* Predict function for a variant in CDS. */
{
char *txName = txSeq->name;
char *gAlt = getGAlt(vpTx, psl);
struct gpFx *fx = gpFxNew(gAlt, txName, coding_sequence_variant, codingChange, lm);
uint pepRefLen = strlen(vpPep->ref);
uint pepAltLen = strlen(vpPep->alt);
char lastPepRef = (pepRefLen > 0) ? vpPep->ref[pepRefLen-1] : '\0';
// Alt pep base at position of last ref pep base, if applicable:
char lastPepRefAlt = (pepRefLen > 0 && pepRefLen <= pepAltLen) ? vpPep->alt[pepRefLen-1] : '\0';
if (vpPep->start == 0 && vpPep->ref[0] == 'M')
    fx->soNumber = initiator_codon_variant;
else if (sameString(vpPep->alt, "X") && differentString(vpPep->ref, "X"))
    fx->soNumber = stop_gained;
else if (sameString(vpPep->alt, "X") && sameString(vpPep->ref, "X"))
    fx->soNumber = stop_retained_variant;
else if (vpPep->frameshift)
    fx->soNumber = frameshift_variant;
else if (endsWith(vpPep->alt, "X") && lastPepRef != 'X')
    fx->soNumber = stop_gained;
else if (lastPepRef == 'X' && lastPepRefAlt != 0)
    {
    // Stop codon variant -- did it actually change?
    if (lastPepRefAlt != 'X')
        fx->soNumber = stop_lost;
    else
        fx->soNumber = stop_retained_variant;
    }
else if (sameString(vpPep->ref, vpPep->alt))
    fx->soNumber = synonymous_variant;
else if (pepRefLen > pepAltLen)
    fx->soNumber = inframe_deletion;
else if (pepRefLen < pepAltLen)
    fx->soNumber = inframe_insertion;
else
    fx->soNumber = missense_variant;
setCodingInfo(fx, vpTx, psl, cds, txSeq, vpPep, lm);
return fx;
}

static struct gpFx *vpTxToFxUtrCds(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                                   struct dnaSeq *txSeq, struct vpPep *vpPep,
                                   struct dnaSeq *protSeq, struct lm *lm)
/* Predict function for a variant that spans UTR and CDS -- if it spans tx start, all we can
 * say is it's complicated. */
{
struct gpFx *fxList = NULL;
char *gAlt = getGAlt(vpTx, psl);
int startExonIx = pslBlkIxToExonIx(psl, vpTx->start.aliBlkIx, MIN_INTRON);
int exonCount = pslCountExons(psl, MIN_INTRON);
if (vpTx->start.txOffset < cds->start)
    {
    // _5_prime_UTR_variant
    slAddHead(&fxList, gpFxNew(gAlt, txSeq->name, _5_prime_UTR_variant, none, lm));
    gpFxSetNoncodingInfo(fxList, startExonIx, exonCount, vpTx->start.txOffset,
                         vpTx->txRef, vpTx->txAlt, lm);
    // initiator_codon_variant
    slAddHead(&fxList, gpFxNew(gAlt, txSeq->name, initiator_codon_variant, none, lm));
    gpFxSetNoncodingInfo(fxList, startExonIx, exonCount, vpTx->start.txOffset,
                         vpTx->txRef, vpTx->txAlt, lm);
    }
else if (vpTx->end.txOffset > cds->end)
    {
    // _3_prime_UTR_variant
    slAddHead(&fxList, gpFxNew(gAlt, txSeq->name, _3_prime_UTR_variant, none, lm));
    gpFxSetNoncodingInfo(fxList, startExonIx, exonCount, vpTx->start.txOffset,
                         vpTx->txRef, vpTx->txAlt, lm);
    // Find out what happened to the stop codon
    slAddHead(&fxList, vpTxToFxCds(vpTx, psl, cds, txSeq, vpPep, protSeq, lm));
    }
return fxList;
}

static struct gpFx *addLostExons(struct vpTx *vpTx, struct psl *psl, char *txName, char *gAlt,
                                 struct lm *lm)
/* Call this only if vpTx spans at least one exon or intron.  This returns exon_loss_variant
 * effects if applicable. */
{
struct gpFx *fxList = NULL;
int startBlkIx = vpTx->start.aliBlkIx, endBlkIx = vpTx->end.aliBlkIx;
boolean isRc = pslQStrand(psl) == '-';
if (isRc)
    {
    if (vpTx->start.region == vpIntron)
        startBlkIx++;
    if (vpTx->end.region == vpIntron)
        endBlkIx++;
    }
int startIx = pslBlkIxToExonIx(psl, startBlkIx, MIN_INTRON) + 1;
if (vpTx->start.region == vpUpstream)
    startIx--;
int endIx = pslBlkIxToExonIx(psl, endBlkIx, MIN_INTRON);
if (vpTx->end.region == vpIntron || vpTx->end.region == vpDownstream)
    endIx++;
int exonCount = pslCountExons(psl, MIN_INTRON);
int ix;
for (ix = startIx;  ix < endIx ;  ix++)
    {
    struct gpFx *fx = gpFxNew(gAlt, txName, exon_loss_variant, nonCodingExon, lm);
    // Instead of start.txOffset it would be better to compute the actual exon start coord
    gpFxSetNoncodingInfo(fx, ix, exonCount, vpTx->start.txOffset, vpTx->txRef, vpTx->txAlt, lm);
    slAddHead(&fxList, fx);
    }
slReverse(&fxList);
return fxList;
}

static struct gpFx *fxIntronFromPsl(struct psl *psl, int aliBlkIx, char *txName, char *gAlt,
                                    enum soTerm soTerm, struct lm *lm)
/* Return a gpFx with intron number/count corresponding to aliBlkIx */
{
struct gpFx *fx = gpFxNew(gAlt, txName, soTerm, intron, lm);
int exonCount = pslCountExons(psl, MIN_INTRON);
int exonStartBlkIx = aliBlkIx;
boolean isRc = pslQStrand(psl) == '-';
if (isRc)
    // The exon to the right on the genome is the exon before this intron
    exonStartBlkIx++;
fx->details.intron.intronNumber = pslBlkIxToExonIx(psl, exonStartBlkIx, MIN_INTRON);
fx->details.intron.intronCount = exonCount - 1;
return fx;
}

static struct gpFx *vpTxToFxIntron(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                                   char *txName, struct lm *lm)
/* Return a list of gpFx for intronic variant (splice? exon_loss?) */
{
// Is it also a splice donor/acceptor/region variant?
uint intronStartDistance = vpTx->start.gDistance;
uint intronEndDistance = vpTx->end.intron3Distance;
enum soTerm soTerm = intron_variant;
if (pslNmdTarget(psl, cds, MIN_INTRON))
    soTerm = NMD_transcript_variant;
else if (intronStartDistance <= 1)
    soTerm = splice_donor_variant;
else if (intronEndDistance <= 1)
    soTerm = splice_acceptor_variant;
else if (intronStartDistance <= 7 || intronEndDistance <= 7)
    soTerm = splice_region_variant;
char *gAlt = getGAlt(vpTx, psl);
struct gpFx *fxList = fxIntronFromPsl(psl, vpTx->start.aliBlkIx, txName, gAlt, soTerm, lm);
fxList = slCat(fxList, addLostExons(vpTx, psl, txName, gAlt, lm));
return fxList;
}

static struct gpFx *vpTxToFxExon(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                                 struct dnaSeq *txSeq, struct vpPep *vpPep,
                                 struct dnaSeq *protSeq, struct lm *lm)
/* Variant's start and end are both exonic (possibly not the same exon). */
{
struct gpFx *fxList = NULL;
int startExonIx = 0;
//if (vpTx->start.aliBlkIx >= 0)
    startExonIx = pslBlkIxToExonIx(psl, vpTx->start.aliBlkIx, MIN_INTRON);
int exonCount = pslCountExons(psl, MIN_INTRON);
char *txName = txSeq->name;
char *gAlt = getGAlt(vpTx, psl);
if (cds && cds->end > cds->start)
    {
    // coding transcript exon -- UTR, CDS or both?
    if (vpTx->start.txOffset < cds->start)
        {
        if (vpTx->end.txOffset <= cds->start)
            {
            fxList = gpFxNew(gAlt, txName, _5_prime_UTR_variant, nonCodingExon, lm);
            gpFxSetNoncodingInfo(fxList, startExonIx, exonCount, vpTx->start.txOffset,
                                 vpTx->txRef, vpTx->txAlt, lm);
            }
        else
            fxList = vpTxToFxUtrCds(vpTx, psl, cds, txSeq, vpPep, protSeq, lm);
        }
    else if (vpTx->end.txOffset > cds->end)
        {
        if (vpTx->start.txOffset >= cds->end)
            {
            fxList = gpFxNew(gAlt, txName, _3_prime_UTR_variant, nonCodingExon, lm);
            gpFxSetNoncodingInfo(fxList, startExonIx, exonCount, vpTx->start.txOffset,
                                 vpTx->txRef, vpTx->txAlt, lm);
            }
        else
            fxList = vpTxToFxUtrCds(vpTx, psl, cds, txSeq, vpPep, protSeq, lm);
        }
    else
        {
        fxList = vpTxToFxCds(vpTx, psl, cds, txSeq, vpPep, protSeq, lm);
        }
    if (pslNmdTarget(psl, cds, MIN_INTRON))
        fxList->soNumber = NMD_transcript_variant;
    }
else
    {
    // non-coding exon
    fxList = gpFxNew(gAlt, txName, non_coding_transcript_exon_variant, nonCodingExon, lm);
    gpFxSetNoncodingInfo(fxList, startExonIx, exonCount, vpTx->start.txOffset,
                         vpTx->txRef, vpTx->txAlt, lm);
    }

//#*** TODO: find exonic splice_region_variant

fxList = slCat(fxList, addLostExons(vpTx, psl, txName, gAlt, lm));
return fxList;
}

static struct gpFx *vpTxToFxSingleRegion(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                                         struct dnaSeq *txSeq, struct vpPep *vpPep,
                                         struct dnaSeq *protSeq, struct lm *lm)
/* A simple variant that doesn't straddle an exon edge (i.e. start & end have same region type). */
{
if (vpTx->start.region != vpTx->end.region)
    errAbort("vpTxToFxSingleRegion: call this only when start region == end region (got %s != %s)",
             vpTxRegionToString(vpTx->start.region), vpTxRegionToString(vpTx->end.region));
struct gpFx *fxList = NULL;
enum vpTxRegion region = vpTx->start.region;
char *gAlt = getGAlt(vpTx, psl);
char *txName = txSeq->name;
char *txRef = vpTxGetRef(vpTx);
if (sameString(txRef, vpTx->txAlt))
    fxList = gpFxNew(gAlt, txName, no_sequence_alteration, none, lm);
else if (region == vpUpstream)
    fxList = gpFxNew(gAlt, txName, upstream_gene_variant, none, lm);
else if (region == vpDownstream)
    fxList = gpFxNew(gAlt, txName, downstream_gene_variant, none, lm);
else if (region == vpIntron)
    fxList = slCat(fxList, vpTxToFxIntron(vpTx, psl, cds, txName, lm));
else if (region == vpExon)
    fxList = slCat(fxList, vpTxToFxExon(vpTx, psl, cds, txSeq, vpPep, protSeq, lm));
else
    errAbort("vpTranscriptToGpFx: unrecognized region type %s (%d)",
             vpTxRegionToString(region), region);
return fxList;
}

struct gpFx *vpTxToFxComplex(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                             struct dnaSeq *txSeq, struct vpPep *vpPep, struct dnaSeq *protSeq,
                             struct lm *lm)
/* Predict consequences (to the extent possible) of variants that begin and end in different
 * transcript regions. */
{
struct gpFx *fxList = NULL;
char *gAlt = getGAlt(vpTx, psl);
char *txName = txSeq->name;
if (vpTx->start.region == vpUpstream && vpTx->end.region == vpDownstream)
    fxList = gpFxNew(gAlt, txName, transcript_ablation, none, lm);
else
    {
    if (vpTxPosIsInsertion(&vpTx->start, &vpTx->end))
        {
        // Insertions can have start regions and end regions that would normally be out of order,
        // e.g. start.region == vpExon and end.region == vpUpstream for an insertion before
        // the first tx base, so there's a different ordering to check here.
        // Insertions at intron boundaries don't disrupt the splice site sequence.  So in
        // the spirit of HGVS's "3' exception rule", assume the change is to the exon and
        // don't report a splice hit, i.e. ignore vpIntron here and don't consider intron/exon
        // boundary insertions complex.  (Still note if the insertion could also be up/downstream.)
        if (vpTx->start.region == vpExon || vpTx->end.region == vpExon)
            fxList = slCat(fxList, vpTxToFxExon(vpTx, psl, cds, txSeq, vpPep, protSeq, lm));
        if (vpTx->end.region == vpUpstream)
            {
            fxList = slCat(fxList, gpFxNew(gAlt, txName, upstream_gene_variant, none, lm));
            fxList = slCat(fxList, gpFxNew(gAlt, txSeq->name, complex_transcript_variant,
                                           none, lm));
            }
        if (vpTx->start.region == vpDownstream)
            {
            fxList = slCat(fxList, gpFxNew(gAlt, txName, downstream_gene_variant, none, lm));
            fxList = slCat(fxList, gpFxNew(gAlt, txSeq->name, complex_transcript_variant,
                                           none, lm));
            }
        if (vpTx->start.region == vpUpstream || vpTx->end.region == vpDownstream)
            errAbort("Unexpected combo of start and end region for insertion: "
                     "start==%s, end==%s",
                     vpTxRegionToString(vpTx->start.region), vpTxRegionToString(vpTx->end.region));
        }
    else
        {
        if (vpTx->start.region == vpUpstream)
            {
            fxList = gpFxNew(gAlt, txName, upstream_gene_variant, none, lm);
            if (vpTx->end.region == vpExon)
                fxList = slCat(fxList, vpTxToFxExon(vpTx, psl, cds, txSeq, vpPep, protSeq, lm));
            else if (vpTx->end.region != vpIntron)
                errAbort("vpTxToFxComplex: unexpected end region type %s after start==vpUpstream",
                         vpTxRegionToString(vpTx->end.region));
            }
        else if (vpTx->start.region == vpExon)
            {
            if (vpTx->end.region == vpIntron)
                fxList = fxIntronFromPsl(psl, vpTx->start.aliBlkIx, txName, gAlt,
                                         splice_donor_variant, lm);
            else if (vpTx->end.region == vpDownstream)
                fxList = vpTxToFxExon(vpTx, psl, cds, txSeq, vpPep, protSeq, lm);
            else
                errAbort("vpTxToFxComplex: unexpected end region type %s after start==vpExon",
                         vpTxRegionToString(vpTx->end.region));
            }
        else if (vpTx->start.region == vpIntron)
            {
            if (vpTx->end.region != vpExon && vpTx->end.region != vpDownstream)
                errAbort("vpTxToFxComplex: unexpected end region type %s after start==vpIntron",
                         vpTxRegionToString(vpTx->end.region));
            fxList = fxIntronFromPsl(psl, vpTx->start.aliBlkIx, txName, gAlt,
                                     splice_acceptor_variant, lm);
            }
        else
            errAbort("vpTxToFxComplex: unexpected start region %s",
                     vpTxRegionToString(vpTx->start.region));
        fxList = slCat(fxList, addLostExons(vpTx, psl, txName, gAlt, lm));
        if (vpTx->end.region == vpDownstream)
            fxList = slCat(fxList, gpFxNew(gAlt, txName, downstream_gene_variant, none, lm));
        fxList = slCat(fxList, gpFxNew(gAlt, txSeq->name, complex_transcript_variant, none, lm));
        }
    }
return fxList;
}

struct gpFx *vpTranscriptToGpFx(struct vpTx *vpTx, struct psl *psl, struct genbankCds *cds,
                                struct dnaSeq *txSeq, struct vpPep *vpPep, struct dnaSeq *protSeq,
                                struct lm *lm)
/* Make gpFx functional prediction(s) (SO term & additional data) from vpTx and sequence. */
{
if (vpTx->start.region == vpTx->end.region)
    return vpTxToFxSingleRegion(vpTx, psl, cds, txSeq, vpPep, protSeq, lm);
else
    return vpTxToFxComplex(vpTx, psl, cds, txSeq, vpPep, protSeq, lm);
}
