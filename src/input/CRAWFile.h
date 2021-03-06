/*
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is based on SDR-J
 *    Copyright (C) 2010, 2011, 2012
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *
 *    This file is part of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with welle.io; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __RAW_FILES
#define __RAW_FILES

#include <QSettings>
#include <QString>
#include <QThread>

#include "CVirtualInput.h"
#include "DabConstants.h"
#include "ringbuffer.h"

class QLabel;
class QSettings;
class fileHulp;

// Enum of available input device
enum class CRAWFileFormat {U8, S8, S16LE, S16BE, Unknown};

class CRAWFile : public CVirtualInput, QThread {
public:
    CRAWFile();
    ~CRAWFile(void);

    // Interface methods
    void setFrequency(int32_t Frequency);
    int32_t getSamples(DSPCOMPLEX*, int32_t);
    int32_t getSpectrumSamples(DSPCOMPLEX* V, int32_t size);
    int32_t getSamplesToRead(void);
    bool restart(void);
    void stop(void);
    void reset(void);
    float setGain(int32_t Gain);
    int32_t getGainCount(void);
    void setAgc(bool AGC);
    QString getName(void);
    CDeviceID getID(void);

    // Specific methods
    void setFileName(QString FileName, QString FileFormat);

private:
    QString FileName;
    CRAWFileFormat FileFormat;
    uint8_t IQByteSize;

    virtual void run(void);
    int32_t readBuffer(uint8_t*, int32_t);
    int32_t convertSamples(RingBuffer<uint8_t>* Buffer, DSPCOMPLEX* V, int32_t size);

    RingBuffer<uint8_t>* SampleBuffer;
    RingBuffer<uint8_t>* SpectrumSampleBuffer;
    int32_t bufferSize;
    FILE* filePointer;
    bool readerOK;
    bool readerPausing;
    bool ExitCondition;
    bool ThreadFinished;
    int64_t currPos;
};

#endif
