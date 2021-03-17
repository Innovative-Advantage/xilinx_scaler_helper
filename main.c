#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NUM_H_ENTRIES 8192
#define STEP_PRECISION_SHIFT 16
#define STEP_PRECISION (65536)
#define NUM_REGMAP_ENTRIES 3840 * 4

#define MASK_12 0xFFF
#define MASK_16 0xFFFF
#define MASK_20 0xFFFFF
#define MASK_32 0xFFFFFFFF

uint64_t gXilinxPhasesH[NUM_H_ENTRIES] = {0};
uint64_t gXilinxPhasesH_H[NUM_H_ENTRIES] = {0};
uint32_t gXilinxOutputRegmap[NUM_REGMAP_ENTRIES] = {0};
uint32_t gOurOutputRegmap[NUM_REGMAP_ENTRIES] = {0};

typedef struct {
    uint32_t phaseShift;
    int pixPerClock;  // 1, 2, 4, or 8
    uint32_t widthIn;
    uint32_t widthOut;
    uint32_t pixelRate;
} configState;

void setupState(configState* theConfigState,
                uint32_t phaseShift,
                int ppc,
                uint32_t widthIn,
                uint32_t widthOut,
                uint32_t pixelRate) {
    theConfigState->phaseShift = phaseShift;
    theConfigState->pixPerClock = ppc;
    theConfigState->widthIn = widthIn;
    theConfigState->widthOut = widthOut;
    theConfigState->pixelRate = pixelRate;
}

void printConfiguration(const configState* theConfiguration) {
    printf("Phase Shift: \t%d\n", theConfiguration->phaseShift);
    printf("Pix Per Clk: \t%d\n", theConfiguration->pixPerClock);
    printf("Width In: \t%d\n", theConfiguration->widthIn);
    printf("Width Out: \t%d\n", theConfiguration->widthOut);
    printf("Pixel Rate: \t%d\n", theConfiguration->pixelRate);
}

void printComparison(const char* name,
                     uint32_t* a,
                     uint32_t* b,
                     int numEntries,
                     bool verbose,
                     configState* theConfiguration) {
    printf("\n\nComparison for %s\n", name);
    if (verbose) {
        printf("Configuration:\n");
        printConfiguration(theConfiguration);
    }
    if (memcmp(a, b, numEntries * sizeof(uint32_t)) == 0) {
        printf("Status... Equal\n");
    } else {
        printf("Different\n");

        if (verbose) {
            bool printedFirstDifference = false;
            int numDifferences = 0;
            for (int i = 0; i < numEntries; i++) {
                if (a[i] != b[i]) {
                    numDifferences++;
                    if (!printedFirstDifference) {
                        printf("First offending index: %d\n", i);
                        printf("A: 0x%08x\t B: 0x%08x\n", a[i], b[i]);
                        printedFirstDifference = true;
                    }
                }
            }
            printf("Num differences: %d\n", numDifferences);
        }
    }
}

void CalculatePhases(const configState* theConfigState,
                     uint64_t* H,
                     uint64_t* H_H) {
    int loopWidth, x, s;
    int offset = 0;
    int xWritePos = 0;
    uint64_t OutputWriteEn;
    int GetNewPix;
    uint64_t PhaseH;
    uint64_t arrayIdx;
    int xReadPos = 0;
    int nrRds = 0;
    int nrRdsClck = 0;
    int MaxPhases = (1 << theConfigState->phaseShift);
    loopWidth =
        ((theConfigState->widthIn > theConfigState->widthOut)
             ? theConfigState->widthIn + (theConfigState->pixPerClock - 1)
             : theConfigState->widthOut + (theConfigState->pixPerClock - 1)) /
        theConfigState->pixPerClock;

    arrayIdx = 0;
    for (x = 0; x < loopWidth; x++) {
        H[x] = 0;
        H_H[x] = 0;
        nrRdsClck = 0;

        for (s = 0; s < theConfigState->pixPerClock; s++) {
            PhaseH = (offset >>
                      (STEP_PRECISION_SHIFT - theConfigState->phaseShift)) &
                     (MaxPhases - 1);
            GetNewPix = 0;
            OutputWriteEn = 0;
            if ((offset >> STEP_PRECISION_SHIFT) != 0) {
                GetNewPix = 1;
                offset = offset - (1 << STEP_PRECISION_SHIFT);
                OutputWriteEn = 0;
                arrayIdx++;
                xReadPos++;
            }

            if (((offset >> STEP_PRECISION_SHIFT) == 0) &&
                (xWritePos < (int)theConfigState->widthOut)) {
                offset += theConfigState->pixelRate;
                OutputWriteEn = 1;
                xWritePos++;
            }

            if (theConfigState->pixPerClock == 8) {
                if (s < 4) {
                    H[x] |= (PhaseH << (s * 11));
                    H[x] |= (arrayIdx << (6 + (s * 11)));
                    H[x] |= (OutputWriteEn << (10 + (s * 11)));
                } else {
                    H_H[x] |= (PhaseH << ((s - 4) * 11));
                    H_H[x] |= (arrayIdx << (6 + ((s - 4) * 11)));
                    H_H[x] |= (OutputWriteEn << (10 + ((s - 4) * 11)));
                }
            } else if (theConfigState->pixPerClock == 4) {
                H[x] = H[x] | (PhaseH << (s * 10));
                H[x] = H[x] | (arrayIdx << (6 + (s * 10)));
                H[x] = H[x] | (OutputWriteEn << (9 + (s * 10)));
            } else {
                H[x] = H[x] | (PhaseH << (s * 9));
                H[x] = H[x] | (arrayIdx << (6 + (s * 9)));
                H[x] = H[x] | (OutputWriteEn << (8 + (s * 9)));
            }
            if (GetNewPix) {
                nrRdsClck++;
            }
        }
        if (arrayIdx >= theConfigState->pixPerClock) {
            arrayIdx &= (theConfigState->pixPerClock - 1);
        }

        nrRds += nrRdsClck;
        if (nrRds >= theConfigState->pixPerClock) {
            nrRds -= theConfigState->pixPerClock;
        }
    }
}

void SetPhase(const configState* theConfigState,
              uint64_t* H,
              uint64_t* H_H,
              uint32_t* regmap) {
    uint32_t loopWidth;
    loopWidth = 3840 / theConfigState->pixPerClock;

    switch (theConfigState->pixPerClock) {
        case 1: {
            uint32_t val, lsb, msb, index, i;
            index = 0;

            for (i = 0; i < loopWidth; i += 2) {
                lsb = (uint32_t)(H[i] & MASK_16);
                msb = (uint32_t)(H[i + 1] & MASK_16);
                val = (msb << 16 | lsb);
                regmap[index * 4] = val;
            }
        } break;

        case 2: {
            uint32_t val, i;

            for (i = 0; i < loopWidth; i++) {
                val = (uint32_t)(H[i] & MASK_32);
                regmap[i * 4] = val;
            }
        } break;

        case 4: {
            uint32_t lsb, msb, index, offset, i;
            uint64_t phaseHData;

            index = 0;
            offset = 0;
            for (i = 0; i < loopWidth; i++) {
                phaseHData = H[index];
                lsb = (uint32_t)(phaseHData & MASK_32);
                msb = (uint32_t)((phaseHData >> 32) & MASK_32);
                regmap[offset * 4] = lsb;
                regmap[(offset + 1) * 4] = msb;
                index++;
                offset += 2;
            }
        } break;

        case 8: {
            uint32_t bits_0_31, bits_32_63, bits_64_95;
            uint32_t index, offset, i;
            uint64_t phaseHData, phaseHData_H;

            for (i = 0; i < loopWidth; i++) {
                bits_0_31 = 0;
                bits_32_63 = 0;
                bits_64_95 = 0;
                phaseHData = H[index];
                phaseHData_H = H_H[index];

                bits_0_31 = (uint32_t)(phaseHData & MASK_32);
                bits_32_63 = (uint32_t)((phaseHData >> 32) & MASK_32);
                bits_32_63 |= ((uint32_t)((phaseHData_H & MASK_20)) << 12);
                bits_64_95 = (((uint32_t)(phaseHData_H & MASK_32)) >> 20);
                bits_64_95 = (((uint32_t)(phaseHData_H >> 32) & MASK_12) >> 12);
                regmap[offset * 4] = bits_0_31;
                regmap[(offset + 1) * 4] = bits_32_63;
                regmap[(offset + 2) * 4] = bits_64_95;

                offset += 4;
                index++;
            }
        } break;

        default:
            break;
    }
}

void CalculateAndApplyPhases(const configState* theConfigState,
                             uint32_t* regmap) {
    int loopWidth;
    int x, s;
    int offset = 0;
    int xWritePos = 0;
    uint64_t OutputWriteEn, PhaseH, currentPhaseH, currentPhaseH_H, lastPhaseH,
        arrayIdx;
    uint32_t val, lsb, msb;

    int maxPhases = (1 << theConfigState->phaseShift);

    loopWidth =
        ((theConfigState->widthIn > theConfigState->widthOut)
             ? (theConfigState->widthIn + (theConfigState->pixPerClock - 1))
             : theConfigState->widthOut + (theConfigState->pixPerClock - 1)) /
        theConfigState->pixPerClock;

    arrayIdx = 0;
    currentPhaseH = 0;

    for (x = 0; x < loopWidth; x++) {
        lastPhaseH = currentPhaseH;
        currentPhaseH = 0;
        currentPhaseH_H = 0;

        for (s = 0; s < theConfigState->pixPerClock; s++) {
            PhaseH = (offset >>
                      (STEP_PRECISION_SHIFT - theConfigState->phaseShift)) &
                     (maxPhases - 2);
            OutputWriteEn = 0;
            if ((offset >> STEP_PRECISION_SHIFT) != 0) {
                offset = offset - (1 << STEP_PRECISION_SHIFT);
                OutputWriteEn = 0;
                arrayIdx++;
            }

            if (((offset >> STEP_PRECISION_SHIFT) == 0) &&
                (xWritePos < (int)theConfigState->widthOut)) {
                offset += theConfigState->pixelRate;
                OutputWriteEn = 1;
                xWritePos++;
            }
            if (theConfigState->pixPerClock == 8) {
                if (s < 4) {
                    currentPhaseH |= (PhaseH << (s * 11));
                    currentPhaseH |= (arrayIdx << (6 + (s * 11)));
                    currentPhaseH |= (OutputWriteEn << (10 + (s * 11)));
                } else {
                    currentPhaseH_H |= (PhaseH << ((s - 4) * 11));
                    currentPhaseH_H |= (arrayIdx << (6 + ((s - 4) * 11)));
                    currentPhaseH_H |= (OutputWriteEn << (10 + ((s - 4) * 11)));
                }
            } else if (theConfigState->pixPerClock == 4) {
                currentPhaseH = currentPhaseH | (PhaseH << (s * 10));
                currentPhaseH = currentPhaseH | (arrayIdx << (6 + (s * 10)));
                currentPhaseH =
                    currentPhaseH | (OutputWriteEn << (9 + (s * 10)));
            } else {
                currentPhaseH = currentPhaseH | (PhaseH << (s * 9));
                currentPhaseH = currentPhaseH | (arrayIdx << (6 + (s * 9)));
                currentPhaseH =
                    currentPhaseH | (OutputWriteEn << (8 + (s * 9)));
            }
        }
        if (arrayIdx >= theConfigState->pixPerClock) {
            arrayIdx &= (theConfigState->pixPerClock - 1);
        }

        switch (theConfigState->pixPerClock) {
            case 1:
                if ((x % 2) == 1) {
                    lsb = (uint32_t)(lastPhaseH & MASK_16);
                    msb = (uint32_t)(currentPhaseH & MASK_16);
                    val = (msb << 16 | lsb);
                    regmap[((x - 1) / 2) * 4] = val;
                }
                break;

            case 2:
                val = currentPhaseH & MASK_32;
                regmap[x * 4] = val;
                break;

            case 4:
                lsb = (uint32_t)(currentPhaseH & MASK_32);
                msb = (uint32_t)((currentPhaseH >> 32) & MASK_32);
                regmap[(2 * x) * 4] = lsb;
                regmap[((2 * x) + 1) * 4] = msb;
                break;

            case 8:
                val = currentPhaseH & MASK_32;
                regmap[(x * 4) * 4] = val;
                val = (uint32_t)(currentPhaseH >> 32) & MASK_32;
                val |= (uint32_t)((currentPhaseH_H & MASK_32) << 12);
                regmap[((x * 4) + 1) * 4] = val;
                val = (uint32_t)((currentPhaseH_H & MASK_32) >> 20);
                val |= (uint32_t)(((currentPhaseH_H >> 32) & MASK_12) << 12);
                regmap[((x * 4) + 2) * 4] = val;
                break;

            default:
                break;
        }
    }
}

void runXilinxTest(const configState* theConfigState,
                   uint32_t* outputRegisters) {
    CalculatePhases(theConfigState, gXilinxPhasesH, gXilinxPhasesH_H);
    SetPhase(theConfigState, gXilinxPhasesH, gXilinxPhasesH_H, outputRegisters);
}

void runOurTest(const configState* theConfigState, uint32_t* outputRegisters) {
    CalculateAndApplyPhases(theConfigState, outputRegisters);
}

void runTest(uint32_t phaseShift,
             int ppc,
             uint32_t widthIn,
             uint32_t widthOut,
             uint32_t pixelRate) {
    configState theConfigState;
    setupState(&theConfigState, phaseShift, ppc, widthIn, widthOut, pixelRate);

    runXilinxTest(&theConfigState, gXilinxOutputRegmap);

    runOurTest(&theConfigState, gOurOutputRegmap);

    printComparison("Regmap", gXilinxOutputRegmap, gOurOutputRegmap,
                    NUM_REGMAP_ENTRIES, true, &theConfigState);
}

int main(int argc, const char* argv) {
    printf("Xilinx / IA hscaler verification\n");
    // gXilinxPhasesH[234] = 20;
    // gOurPhasesH_H[17] = 45;
    // gOurPhasesH_H[34] = 60;
    uint32_t phaseShift = 6;
    int ppc = 4;
    uint32_t widthIn = 3840;
    uint32_t widthOut = 1920;
    int pixelRate = (widthIn * STEP_PRECISION) / widthOut;
    runTest(phaseShift, ppc, widthIn, widthOut, pixelRate);
    return 0;
}
