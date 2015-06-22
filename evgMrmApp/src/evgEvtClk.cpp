#include "evgEvtClk.h"

#include <stdio.h>
#include <errlog.h> 
#include <stdexcept>

#include <mrfCommonIO.h> 
#include <mrfCommon.h> 
#include <mrfFracSynth.h>

#include "evgRegMap.h"

evgEvtClk::evgEvtClk(const std::string& name, volatile epicsUInt8* const pReg):
mrf::ObjectInst<evgEvtClk>(name),
m_pReg(pReg),
m_RFref(0.0f),
m_fracSynFreq(0.0f) {
}

evgEvtClk::~evgEvtClk() {
}

epicsFloat64
evgEvtClk::getFrequency() const {
    if(getSource() == ClkSrcInternal)
        return m_fracSynFreq;
    else
        return getRFFreq()/getRFDiv();
}

void
evgEvtClk::setRFFreq (epicsFloat64 RFref) {
    if(RFref < 50.0f || RFref > 1600.0f) {
        char err[80];
        sprintf(err, "Cannot set RF frequency to %f MHz. Valid range is 50 - 1600.", RFref);
        std::string strErr(err);
        throw std::runtime_error(strErr);
    }

    m_RFref = RFref;
}

epicsFloat64
evgEvtClk::getRFFreq() const {
    return m_RFref;    
}

void
evgEvtClk::setRFDiv(epicsUInt32 rfDiv) {
    if(rfDiv < 1    || rfDiv > 32) {
        char err[80];
        sprintf(err, "Invalid RF Divider %d. Valid range is 1 - 32", rfDiv);
        std::string strErr(err);
        throw std::runtime_error(strErr);
    }
    
    WRITE8(m_pReg, RfDiv, rfDiv-1);
}

epicsUInt32
evgEvtClk::getRFDiv() const {
    return READ8(m_pReg, RfDiv) + 1;
}

void
evgEvtClk::setFracSynFreq(epicsFloat64 freq) {
    epicsUInt32 controlWord, oldControlWord;
    epicsFloat64 error;

    controlWord = FracSynthControlWord (freq, MRF_FRAC_SYNTH_REF, 0, &error);
    if ((!controlWord) || (error > 100.0)) {
        char err[80];
        sprintf(err, "Cannot set event clock speed to %f MHz.\n", freq);            
        std::string strErr(err);
        throw std::runtime_error(strErr);
    }

    oldControlWord=READ32(m_pReg, FracSynthWord);

    /* Changing the control word disturbes the phase of the synthesiser
     which will cause a glitch. Don't change the control word unless needed.*/
    if(controlWord != oldControlWord){
        WRITE32(m_pReg, FracSynthWord, controlWord);
        epicsUInt16 uSecDivider = (epicsUInt16)freq;
        WRITE16(m_pReg, uSecDiv, uSecDivider);
    }

    m_fracSynFreq = FracSynthAnalyze(READ32(m_pReg, FracSynthWord), 24.0, 0);
}

epicsFloat64
evgEvtClk::getFracSynFreq() const {
    return FracSynthAnalyze(READ32(m_pReg, FracSynthWord), 24.0, 0);
}

void
evgEvtClk::setSource (epicsUInt16 source) {
    epicsUInt8 clkReg, regMap = 0;

    switch ((RFClockReference) source) {
    case RFClockReference_Internal:
        regMap = EVG_CLK_SRC_INTERNAL;
        break;
    case RFClockReference_External:
        regMap = EVG_CLK_SRC_EXTERNAL;
        break;
    case RFClockReference_PXIe100:
        regMap = EVG_CLK_SRC_PXIE100;
        break;
    case RFClockReference_PXIe10:
        regMap = EVG_CLK_SRC_PXIE10;
        break;
    case RFClockReference_Recovered:
        regMap = EVG_CLK_SRC_RECOVERED;
        break;
    default:
        throw std::out_of_range("RF clock source you selected does not exist.");
        break;
    }

    clkReg = READ8(m_pReg, ClockSource);    // read register content
    clkReg &= ~EVG_CLK_SRC_SEL; // clear old clock source
    clkReg |= regMap;  // set new clock source
    WRITE8(m_pReg, ClockSource, clkReg);    // write the new value to the register
}

epicsUInt16
evgEvtClk::getSource() const {
    epicsUInt8 clkReg = READ8(m_pReg, ClockSource);
    return clkReg & EVG_CLK_SRC_SEL;
}

