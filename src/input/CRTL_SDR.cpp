/*
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is based on SDR-J
 *    Copyright (C) 2010, 2011, 2012, 2013
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

#include <QDebug>

#include "CRTL_SDR.h"

#define READLEN_DEFAULT 8192

//	For the callback, we do need some environment which
//	is passed through the ctx parameter
//
//	This is the user-side call back function
//	ctx is the calling task
static void RTLSDRCallBack(uint8_t* buf, uint32_t len, void* ctx)
{
    CRTL_SDR* RTL_SDR = (CRTL_SDR*)ctx;
    int32_t tmp;

    if ((RTL_SDR == NULL) || (len != READLEN_DEFAULT))
        return;

    tmp = RTL_SDR->SampleBuffer->putDataIntoBuffer(buf, len);
    if ((len - tmp) > 0)
        RTL_SDR->sampleCounter += len - tmp;
    RTL_SDR->SpectrumSampleBuffer->putDataIntoBuffer(buf, len);

    // Check if device is overloaded
    uint8_t MinValue = 255;
    uint8_t MaxValue = 0;

    for(uint32_t i=0;i<len;i++)
    {
        if(MinValue > buf[i])
            MinValue = buf[i];
        if(MaxValue < buf[i])
            MaxValue = buf[i];
    }

    RTL_SDR->setMinMaxValue(MinValue, MaxValue);
}

//	Our wrapper is a simple classs
CRTL_SDR::CRTL_SDR()
{
    int ret = 0;

    qDebug() << "RTL_SDR:" << "Open rtl-sdr";

    open = false;
    isAGC = false;
    RTL_SDR_Thread = NULL;
    lastFrequency = KHz(94700); // just a dummy
    sampleCounter = 0;
    FrequencyOffset = 0;
    gains = NULL;
    CurrentGain = 0;
    CurrentGainCount = 0;
    device = NULL;
    MinValue = 255;
    MaxValue = 0;

    SampleBuffer = new RingBuffer<uint8_t>(1024 * 1024);
    SpectrumSampleBuffer = new RingBuffer<uint8_t>(8192);

    // Get all devices
    uint32_t deviceCount = rtlsdr_get_device_count();
    if (deviceCount == 0)
    {
        qDebug() << "RTL_SDR:" << "No devices found";
        throw 0;
    }
    else
    {
        qDebug() << "RTL_SDR:" << "Found" << deviceCount << "devices. Uses the first one";
    }

    //	Open the first device
    ret = rtlsdr_open(&device, 0);
    if (ret < 0)
    {
        qDebug() << "RTL_SDR:" << "Opening rtl-sdr failed";
        throw 0;
    }

    open = true;

    // Set sample rate
    ret = rtlsdr_set_sample_rate(device, INPUT_RATE);
    if (ret < 0)
    {
        qDebug() << "RTL_SDR:" << "Setting sample rate failed";
        throw 0;
    }

    // Get tuner gains
    GainsCount = rtlsdr_get_tuner_gains(device, NULL);
    qDebug() << "RTL_SDR:" << "Supported gain values" << GainsCount;
    gains = new int[GainsCount];
    GainsCount = rtlsdr_get_tuner_gains(device, gains);

    for (int i = GainsCount; i > 0; i--)
        qDebug() << "RTL_SDR:" << "gain" << (gains[i - 1] / 10.0);

    // Always use manual gain, the AGC is implemented in software
    rtlsdr_set_tuner_gain_mode(device, 1);

    // Enable AGC by default
    setAgc(true);

    connect(&AGCTimer, SIGNAL(timeout(void)), this, SLOT(AGCTimerTimeout(void)));

    return;
}

CRTL_SDR::~CRTL_SDR(void)
{
    if (RTL_SDR_Thread != NULL)
    { // we are running
        rtlsdr_cancel_async(device);
        if (RTL_SDR_Thread != NULL)
        {
            while (!RTL_SDR_Thread->isFinished())
                usleep(100);
            delete RTL_SDR_Thread;
        }
    }

    RTL_SDR_Thread = NULL;

    if (open)
        rtlsdr_close(device);

    if (SampleBuffer != NULL)
        delete SampleBuffer;

    if (SpectrumSampleBuffer != NULL)
        delete SpectrumSampleBuffer;

    if (gains != NULL)
        delete[] gains;

    open = false;
}

void CRTL_SDR::setFrequency(int32_t Frequency)
{
    lastFrequency = Frequency;
    (void)(rtlsdr_set_center_freq(device, Frequency + FrequencyOffset));
}

bool CRTL_SDR::restart(void)
{
    int ret;

    if (RTL_SDR_Thread != NULL)
        return true;

    SampleBuffer->FlushRingBuffer();
    SpectrumSampleBuffer->FlushRingBuffer();
    ret = rtlsdr_reset_buffer(device);
    if (ret < 0)
        return false;

    rtlsdr_set_center_freq(device, lastFrequency + FrequencyOffset);
    RTL_SDR_Thread = new CRTL_SDR_Thread(this);

    AGCTimer.start(50);

    return true;
}

void CRTL_SDR::stop(void)
{
    if (RTL_SDR_Thread == NULL)
        return;

    AGCTimer.stop();

    rtlsdr_cancel_async(device);
    if (RTL_SDR_Thread != NULL) {
        while (!RTL_SDR_Thread->isFinished())
            usleep(100);

        delete RTL_SDR_Thread;
    }

    RTL_SDR_Thread = NULL;
}

float CRTL_SDR::setGain(int32_t Gain)
{
    if(Gain >= GainsCount)
    {
        qDebug() << "RTL_SDR:" << "Unknown gain count" << Gain;
        return 0;
    }

    CurrentGainCount = Gain;
    CurrentGain = gains[Gain];
    rtlsdr_set_tuner_gain(device, CurrentGain);

    //qDebug() << "RTL_SDR:" << "Set gain to" << theGain / 10.0 << "db";

    return CurrentGain / 10.0;
}

int32_t CRTL_SDR::getGainCount()
{
    return GainsCount - 1;
}

void CRTL_SDR::setAgc(bool AGC)
{
    if (AGC == true)
    {
        isAGC = true;
    }
    else
    {
        isAGC = false;
        setGain(CurrentGainCount);
    }
}

QString CRTL_SDR::getName()
{
    char manufact[256] = {0};
    char product[256] = {0};
    char serial[256] = {0};

    rtlsdr_get_usb_strings(device, manufact, product, serial);

    return QString(manufact) + ", " + QString(product) + ", " + QString(serial);
}

CDeviceID CRTL_SDR::getID()
{
    return CDeviceID::RTL_SDR;
}

void CRTL_SDR::setMinMaxValue(uint8_t MinValue, uint8_t MaxValue)
{
    this->MinValue = MinValue;
    this->MaxValue = MaxValue;
}

void CRTL_SDR::AGCTimerTimeout(void)
{
    if(isAGC)
    {
        // Check for overloading
        if(MinValue == 0 || MaxValue == 255)
        {
            // We have to decrease the gain
            setGain(CurrentGainCount - 1);

            qDebug() << "RTL_SDR:" << "Decrease gain to" << (float) CurrentGain / 10;
        }
        else
        {            
            if(CurrentGainCount < (GainsCount - 1))
            {
                // Calc if a gain increase overloads the device. Calc it from the gain values
                int NewGain = gains[CurrentGainCount + 1];
                float DeltaGain = ((float) NewGain / 10) - ((float) CurrentGain / 10);
                float LinGain = pow(10, DeltaGain / 20);

                int NewMaxValue = (float) MaxValue * LinGain;
                int NewMinValue = (float) MinValue / LinGain;

                // We have to increase the gain
                if(NewMinValue >=0 && NewMaxValue <= 255)
                {
                    setGain(CurrentGainCount + 1);
                    qDebug() << "RTL_SDR:" << "Increase gain to" << (float) CurrentGain / 10;
                }
            }
        }
    }
    else // AGC is off
    {
        if(MinValue == 0 || MaxValue == 255)
            qDebug() << "RTL_SDR:" << "ADC overload. Maybe you are using a to high gain.";
    }
}

int32_t CRTL_SDR::getSamples(DSPCOMPLEX* Buffer, int32_t Size)
{
    uint8_t* tempBuffer = (uint8_t*)alloca(2 * Size * sizeof(uint8_t));

    // Get samples
    int32_t amount = this->SampleBuffer->getDataFromBuffer(tempBuffer, 2 * Size);

    // Convert samples into generic format
    for (int i = 0; i < amount / 2; i++)
        Buffer[i] = DSPCOMPLEX((float(tempBuffer[2 * i] - 128)) / 128.0, (float(tempBuffer[2 * i + 1] - 128)) / 128.0);

    return amount / 2;
}

int32_t CRTL_SDR::getSpectrumSamples(DSPCOMPLEX* Buffer, int32_t Size)
{
    uint8_t* tempBuffer = (uint8_t*)alloca(2 * Size * sizeof(uint8_t));

    // Get samples
    int32_t amount = SpectrumSampleBuffer->getDataFromBuffer(tempBuffer, 2 * Size);

    // Convert samples into generic format
    for (int i = 0; i < amount / 2; i++)
        Buffer[i] = DSPCOMPLEX((float(tempBuffer[2 * i] - 128)) / 128.0, (float(tempBuffer[2 * i + 1] - 128)) / 128.0);

    return amount / 2;
}

int32_t CRTL_SDR::getSamplesToRead(void)
{
    return SampleBuffer->GetRingBufferReadAvailable() / 2;
}

void CRTL_SDR::reset(void)
{
    SampleBuffer->FlushRingBuffer();
}

// CRTL_SDR_Thread
CRTL_SDR_Thread::CRTL_SDR_Thread(CRTL_SDR *RTL_SDR)
{
    this->RTL_SDR = RTL_SDR;
    start();
}

CRTL_SDR_Thread::~CRTL_SDR_Thread() {}

void CRTL_SDR_Thread::run()
{
    rtlsdr_read_async(RTL_SDR->device,
                      (rtlsdr_read_async_cb_t)&RTLSDRCallBack,
                      (void*)RTL_SDR, 0, READLEN_DEFAULT);
}
