/*
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
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
 *    Driver for https://github.com/martinmarinov/rtl_tcp_andro-
 */

#include "CAndroid_RTL_SDR.h"

#define RESULT_OK -1

CAndroid_RTL_SDR::CAndroid_RTL_SDR() : CRTL_TCP_Client()
{
    this->isLoaded = false;

    // Start Android rtl_tcp
    QAndroidJniObject path = QAndroidJniObject::fromString("iqsrc://-a 127.0.0.1 -p 1234 -s 2048000");

    QAndroidJniObject uri = QAndroidJniObject::callStaticObjectMethod(
                "android/net/Uri",
                "parse",
                "(Ljava/lang/String;)Landroid/net/Uri;", path.object<jstring>());

    QAndroidJniObject ACTION_VIEW = QAndroidJniObject::getStaticObjectField<jstring>(
                "android/content/Intent",
                "ACTION_VIEW");

    QAndroidJniObject intent(
                "android/content/Intent",
                "(Ljava/lang/String;)V",
                ACTION_VIEW.object<jstring>());

    QAndroidJniObject result = intent.callObjectMethod(
                "setData",
                "(Landroid/net/Uri;)Landroid/content/Intent;",
                uri.object<jobject>());

    resultReceiver = new ActivityResultReceiver(this);
    QtAndroid::startActivity(intent, 1, resultReceiver);

    // Configure rtl_tcp_client
    setIP("127.0.0.1");
    setPort(1234);
}

CAndroid_RTL_SDR::~CAndroid_RTL_SDR()
{
    delete resultReceiver;
}

QString CAndroid_RTL_SDR::getName()
{
    return "Android rtl-sdr " + message;
}

CDeviceID CAndroid_RTL_SDR::getID()
{
    return CDeviceID::ANDROID_RTL_SDR;
}

bool CAndroid_RTL_SDR::restart()
{
    if(isLoaded)
        return CRTL_TCP_Client::restart();
    else
        return false;
}


void CAndroid_RTL_SDR::setMessage(QString message)
{
    this->message = message;
}

void CAndroid_RTL_SDR::setLoaded(bool isLoaded)
{
    this->isLoaded = isLoaded;
}

void ActivityResultReceiver::handleActivityResult(int receiverRequestCode, int resultCode, const QAndroidJniObject &data)
{
    if(receiverRequestCode == 1)
    {
        if(resultCode == RESULT_OK)
        {
            qDebug() << "Android RTL_SDR: Successfully opened";
            Android_RTL_SDR->setLoaded(true);
        }
        else
        {
            QAndroidJniObject MessageType = QAndroidJniObject::fromString("detailed_exception_message");

            QAndroidJniObject result = data.callObjectMethod(
                        "getStringExtra",
                        "(Ljava/lang/String;)Ljava/lang/String;",
                        MessageType.object<jstring>());

            QString Message = result.toString();
            qDebug() << "Android RTL_SDR:" << Message;

            Android_RTL_SDR->setMessage(Message);
        }
    }
}
