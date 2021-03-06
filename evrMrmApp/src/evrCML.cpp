/*************************************************************************\
* Copyright (c) 2010 Brookhaven Science Associates, as Operator of
*     Brookhaven National Laboratory.
* mrfioc2 is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/
/*
 * Author: Michael Davidsaver <mdavidsaver@bnl.gov>
 */

#include <stdexcept>
#include <algorithm>

#include <epicsMath.h>

#include <mrfCommonIO.h>
#include <mrfBitOps.h>
#include "evrRegMap.h"


#include "evrMrm.h"
#include "evrCML.h"

EvrCML::EvrCML(const std::string& n, size_t i, EVRMRM& o, outkind k)
  :mrf::ObjectInst<EvrCML>(n)
  ,base(o.base)
  ,N(i)
  ,owner(o)
  ,shadowEnable(0)
  ,shadowWaveformlength(0)
  ,kind(k)
  ,deviceInfo(o.getDeviceInfo())
{
    if(deviceInfo->getFormFactor() == mrmDeviceInfo::formFactor_CPCIFULL || deviceInfo->getFirmwareId() == mrmDeviceInfo::firmwareId_delayCompensation) {
        mult = 40;
        wordlen = 2;
    }else{
        mult = 20;
        wordlen = 1;
    }

    epicsUInt32 val=READ32(base, OutputCMLEna(N));

    val&=~OutputCMLEna_type_mask;

    switch(kind) {
    case typeCML: break;
    case typeTG203: val|=OutputCMLEna_type_203; break;
    case typeTG300: val|=OutputCMLEna_type_300; break;
    default:
        throw std::invalid_argument("Invalid CML kind");
    }

    for(size_t i=0; i<NELEMENTS(shadowPattern); i++) {
        epicsUInt32 L = wordlen * (lenPatternMax((pattern)i)/mult);
        shadowPattern[i] = new epicsUInt32[L];
        std::fill(shadowPattern[i], shadowPattern[i]+L, 0);
    }

    shadowEnable=val;
}

EvrCML::~EvrCML()
{
    for(size_t i=0; i<NELEMENTS(shadowPattern); i++)
        delete[] shadowPattern[i];
}

void EvrCML::lock() const{owner.lock();};
void EvrCML::unlock() const{owner.unlock();};

cmlMode
EvrCML::mode() const
{
    switch(shadowEnable&OutputCMLEna_mode_mask) {
    case OutputCMLEna_mode_orig:
        return cmlModeOrig;
    case OutputCMLEna_mode_freq:
        return cmlModeFreq;
    case OutputCMLEna_mode_patt:
        return cmlModePattern;
    default:
        return cmlModeInvalid;
    }
}

void
EvrCML::setMode(cmlMode m)
{
    epicsUInt32 mask=0;
    switch(m) {
    case cmlModeOrig:    mask |= OutputCMLEna_mode_orig; break;
    case cmlModeFreq:    mask |= OutputCMLEna_mode_freq; break;
    case cmlModePattern: mask |= OutputCMLEna_mode_patt; break;
    default:
        throw std::out_of_range("Invalid CML Mode");
    }
    bool wasenabled = enabled();
    shadowEnable &= ~OutputCMLEna_ena; // disable while syncing
    shadowEnable &= ~OutputCMLEna_mode_mask;
    shadowEnable |= mask;
    WRITE32(base, OutputCMLEna(N), shadowEnable);


    switch(m) {
    case cmlModeOrig:
        WRITE32(base, OutputCMLPatLength(N), 0);
        syncPattern(patternFall);
        syncPattern(patternHigh);
        syncPattern(patternLow);
        syncPattern(patternRise);
        break;

    case cmlModePattern:
        WRITE32(base, OutputCMLPatLength(N), shadowWaveformlength-1);
        syncPattern(patternWaveform);
        break;

    default:
        break;
    }

    if(wasenabled)
        shadowEnable |= OutputCMLEna_ena; // enable after syncing
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

bool
EvrCML::enabled() const
{
    return shadowEnable&OutputCMLEna_ena;
}

void
EvrCML::enable(bool s)
{
    if(s)
        shadowEnable |= OutputCMLEna_ena;
    else
        shadowEnable &= ~OutputCMLEna_ena;
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

bool
EvrCML::inReset() const
{
    return (shadowEnable & OutputCMLEna_rst) != 0;
}

void
EvrCML::reset(bool s)
{
    if(s)
        shadowEnable |= OutputCMLEna_rst;
    else
        shadowEnable &= ~OutputCMLEna_rst;
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

bool
EvrCML::powered() const
{
    return !(shadowEnable & OutputCMLEna_pow);
}

void
EvrCML::power(bool s)
{
    if(!s)
        shadowEnable |= OutputCMLEna_pow;
    else
        shadowEnable &= ~OutputCMLEna_pow;
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

double
EvrCML::fineDelay() const
{
    // The GTX output fine delay is an external chip
    // and not related to the clock frequency.
    // So just scale it to [0, 1) and use ESLO for the
    // actual calibration
    return READ32(base, GTXDelay(N))/1024.0;
}

void
EvrCML::setFineDelay(double v)
{
    if(v>1024.0){
        printf("Delay will be set to 1024 instead of %f\n", v);
        v=1024.0;
    }
    WRITE32(base, GTXDelay(N), roundToUInt(v*1024.0));
}

void
EvrCML::setPolarityInvert(bool s)
{
    if(s)
        shadowEnable |= OutputCMLEna_ftrg;
    else
        shadowEnable &= ~OutputCMLEna_ftrg;
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

bool
EvrCML::polarityInvert() const
{
    return (shadowEnable & OutputCMLEna_ftrg) != 0;
}

epicsUInt32
EvrCML::countHigh() const
{
    epicsUInt32 val = READ32(base, OutputCMLCount(N));
    val >>= OutputCMLCount_high_shft;
    return val & OutputCMLCount_mask;
}

epicsUInt32
EvrCML::countLow () const
{
    epicsUInt32 val = READ32(base, OutputCMLCount(N));
    val >>= OutputCMLCount_low_shft;
    return val & OutputCMLCount_mask;
}


epicsUInt32
EvrCML::countInit () const
{
    epicsUInt32 v = shadowEnable & OutputCMLEna_ftrig_mask;
    return v >> OutputCMLEna_ftrig_shft;
}

void
EvrCML::setCountHigh(epicsUInt32 v)
{
    if(v<=20 || v>=65535){
        throw std::out_of_range("Invalid CML freq. count");
    }

    epicsUInt32 val = READ32(base, OutputCMLCount(N));
    val &= ~(OutputCMLCount_mask << OutputCMLCount_high_shft);
    val |= v << OutputCMLCount_high_shft;
    WRITE32(base, OutputCMLCount(N), val);
}

void
EvrCML::setCountLow (epicsUInt32 v)
{
    if(v<=20 || v>=65535)
        throw std::out_of_range("Invalid CML freq. count");

    epicsUInt32 val = READ32(base, OutputCMLCount(N));
    val &= ~(OutputCMLCount_mask << OutputCMLCount_low_shft);
    val |= v << OutputCMLCount_low_shft;
    WRITE32(base, OutputCMLCount(N), val);
}

void
EvrCML::setCountInit (epicsUInt32 v)
{
    if(v>=65535)
        throw std::out_of_range("Invalid CML freq. count");

    v <<= OutputCMLEna_ftrig_shft;
    shadowEnable &= ~OutputCMLEna_ftrig_mask;
    shadowEnable |= v;
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

double
EvrCML::timeHigh() const
{
    double period=1.0/(mult*owner.clock());

    return countHigh()*period;
}

double
EvrCML::timeLow () const
{
    double period=1.0/(mult*owner.clock());

    return countLow()*period;
}

double
EvrCML::timeInit () const
{
    double period=1.0/(mult*owner.clock());

    return countInit()*period;
}

void
EvrCML::setTimeHigh(double v)
{
    double period=1.0/(mult*owner.clock());

    setCountHigh(roundToUInt(v/period));
}

void
EvrCML::setTimeLow (double v)
{
    double period=1.0/(mult*owner.clock());

    setCountLow(roundToUInt(v/period));
}

void
EvrCML::setTimeInit (double v)
{
    double period=1.0/(mult*owner.clock());

    setCountInit(roundToUInt(v/period));
}

  // For Pattern mode

bool
EvrCML::recyclePat() const
{
    return (shadowEnable & OutputCMLEna_cycl) != 0;
}

void
EvrCML::setRecyclePat(bool s)
{
    if(s)
        shadowEnable |= OutputCMLEna_cycl;
    else
        shadowEnable &= ~OutputCMLEna_cycl;
    WRITE32(base, OutputCMLEna(N), shadowEnable);
}

epicsUInt32
EvrCML::lenPattern(pattern p) const
{
    if(p==patternWaveform)
        return mult*shadowWaveformlength;
    else
        return mult;
}

epicsUInt32
EvrCML::lenPatternMax(pattern p) const
{
    if(p==patternWaveform)
        return mult*OutputCMLPatLengthMax;
    else
        return mult;
}

epicsUInt32
EvrCML::getPattern(pattern p, unsigned char *buf, epicsUInt32 blen) const
{
    epicsUInt32 plen=lenPattern(p);

    // number of bytes of 'buf' to fill
    blen = std::min(plen, blen);

    epicsUInt32 val=0;
    for(epicsUInt32 i=0; i<blen; i++) {
        size_t cmlword = (i/mult);
        size_t cmlbit  = (i%mult);

        size_t cpuword, cpubit;
        bool first; // first bit in CPU word

        if(mult<32) {
            first = cmlbit==0;
            cpuword = cmlword;
            cpubit = 19 - cmlbit;
        } else {
            first = cmlbit==0 || cmlbit==8;
            cpuword = 2*cmlword + (cmlbit<8 ? 0 : 1);
            cpubit  = cmlbit<8 ? 7-cmlbit : 31-(cmlbit-8);
        }

        if(first) {
            val=shadowPattern[p][cpuword];
        }

        buf[i]=val>>cpubit;
        buf[i]&=0x1;

    }
    return blen;
}

void
EvrCML::setPattern(pattern p, const unsigned char *buf, epicsUInt32 blen)
{
    // If we are given a length that is not a multiple of CML word size
    // then truncate.
    if(blen%mult){
        printf("Given length %u is not a multiple of %u (CML word size).", blen, mult);
        blen-=blen%mult;
        printf("Truncating to %u\n", blen);
    }

    if(blen>lenPatternMax(p))
        throw std::out_of_range("Pattern is too long");


    epicsUInt32 val=0;
    for(epicsUInt32 i=0; i<blen; i++) {
        size_t cmlword = (i/mult);
        size_t cmlbit  = (i%mult);
        size_t cpuword, cpubit;
        if(mult<32) {
            cpuword = cmlword;
            cpubit = 19 - cmlbit;
        } else {
            cpuword = 2*cmlword + (cmlbit<8 ? 0 : 1);
            cpubit  = cmlbit<8 ? 7-cmlbit : 31-(cmlbit-8);
        }

        val|=(!!buf[i])<<cpubit;

        // cpubit is counting down.
        // write complete dword after the last bit is set
        if(cpubit==0) {
            shadowPattern[p][cpuword] = val;
            val=0;
        }
    }

    if(p==patternWaveform)
        shadowWaveformlength = blen/mult;

    // temporarly disable when changing multi dword patterns
    // to prevent output of incomplete patterns
    bool active=enabled();
    if(active)
        enable(false);

    if(mode()==cmlModePattern)
        WRITE32(base, OutputCMLPatLength(N), shadowWaveformlength-1);

    syncPattern(p);

    if(active)
        enable(true);
}

void
EvrCML::syncPattern(pattern p)
{
    if(mult==20 && p!=patternWaveform) {
        // for 20 bit patterns modifying the 4x pattern
        // can be done without effecting the waveform pattern
        switch(p) {
        case patternHigh:
            WRITE32(base, OutputCMLHigh(N), shadowPattern[patternHigh][0]); return;
        case patternFall:
            WRITE32(base, OutputCMLFall(N), shadowPattern[patternFall][0]); return;
        case patternLow:
            WRITE32(base, OutputCMLLow(N),  shadowPattern[patternLow][0]);  return;
        case patternRise:
            WRITE32(base, OutputCMLRise(N), shadowPattern[patternRise][0]); return;
        default:
            throw std::logic_error("syncPattern: invalid state 20");
        }
    }

    cmlMode curmode=mode();
    if(curmode==cmlModeFreq)
        return; // Don't know which pattern to sync...

    switch(curmode) {
    case cmlModeOrig:

        switch(p) {
        case patternLow:
            WRITE32(base, OutputCMLPat(N, 0), shadowPattern[patternLow][0]);
            WRITE32(base, OutputCMLPat(N, 1), shadowPattern[patternLow][1]);
            break;

        case patternRise:
            WRITE32(base, OutputCMLPat(N, 2), shadowPattern[patternRise][0]);
            WRITE32(base, OutputCMLPat(N, 3), shadowPattern[patternRise][1]);
            break;

        case patternFall:
            WRITE32(base, OutputCMLPat(N, 4), shadowPattern[patternFall][0]);
            WRITE32(base, OutputCMLPat(N, 5), shadowPattern[patternFall][1]);
            break;

        case patternHigh:
            WRITE32(base, OutputCMLPat(N, 6), shadowPattern[patternHigh][0]);
            WRITE32(base, OutputCMLPat(N, 7), shadowPattern[patternHigh][1]);
            break;

        case patternWaveform:
            break; // not safe to sync
        }
        break;

    case cmlModePattern:
        switch(p) {
        case patternWaveform:
            for(size_t i=0; i<shadowWaveformlength*wordlen; i++)
                WRITE32(base, OutputCMLPat(N, i), shadowPattern[patternWaveform][i]);
            break;
        default:
            break; // not safe to sync
        }

        break;

    default:
        throw std::logic_error("syncPattern: invalid state 40");
    }
}
