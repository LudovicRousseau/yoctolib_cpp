/*********************************************************************
 *
 *  $Id: svn_id $
 *
 *  Implements yFindSpectralChannel(), the high-level API for SpectralChannel functions
 *
 *  - - - - - - - - - License information: - - - - - - - - -
 *
 *  Copyright (C) 2011 and beyond by Yoctopuce Sarl, Switzerland.
 *
 *  Yoctopuce Sarl (hereafter Licensor) grants to you a perpetual
 *  non-exclusive license to use, modify, copy and integrate this
 *  file into your software for the sole purpose of interfacing
 *  with Yoctopuce products.
 *
 *  You may reproduce and distribute copies of this file in
 *  source or object form, as long as the sole purpose of this
 *  code is to interface with Yoctopuce products. You must retain
 *  this notice in the distributed source file.
 *
 *  You should refer to Yoctopuce General Terms and Conditions
 *  for additional information regarding your rights and
 *  obligations.
 *
 *  THE SOFTWARE AND DOCUMENTATION ARE PROVIDED 'AS IS' WITHOUT
 *  WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 *  WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO
 *  EVENT SHALL LICENSOR BE LIABLE FOR ANY INCIDENTAL, SPECIAL,
 *  INDIRECT OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA,
 *  COST OF PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY OR
 *  SERVICES, ANY CLAIMS BY THIRD PARTIES (INCLUDING BUT NOT
 *  LIMITED TO ANY DEFENSE THEREOF), ANY CLAIMS FOR INDEMNITY OR
 *  CONTRIBUTION, OR OTHER SIMILAR COSTS, WHETHER ASSERTED ON THE
 *  BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE), BREACH OF
 *  WARRANTY, OR OTHERWISE.
 *
 *********************************************************************/


#define _CRT_SECURE_NO_DEPRECATE //do not use windows secure crt
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "yocto_spectralchannel.h"
#include "yapi/yjson.h"
#include "yapi/yapi.h"
#define  __FILE_ID__  "spectralchannel"

#ifdef YOCTOLIB_NAMESPACE
using namespace YOCTOLIB_NAMESPACE;
#endif

YSpectralChannel::YSpectralChannel(const string& func): YSensor(func)
//--- (YSpectralChannel initialization)
    ,_rawCount(RAWCOUNT_INVALID)
    ,_channelName(CHANNELNAME_INVALID)
    ,_peakWavelength(PEAKWAVELENGTH_INVALID)
    ,_valueCallbackSpectralChannel(NULL)
    ,_timedReportCallbackSpectralChannel(NULL)
//--- (end of YSpectralChannel initialization)
{
    _className="SpectralChannel";
}

YSpectralChannel::~YSpectralChannel()
{
//--- (YSpectralChannel cleanup)
//--- (end of YSpectralChannel cleanup)
}
//--- (YSpectralChannel implementation)
// static attributes
const string YSpectralChannel::CHANNELNAME_INVALID = YAPI_INVALID_STRING;

int YSpectralChannel::_parseAttr(YJSONObject *json_val)
{
    if(json_val->has("rawCount")) {
        _rawCount =  json_val->getInt("rawCount");
    }
    if(json_val->has("channelName")) {
        _channelName =  json_val->getString("channelName");
    }
    if(json_val->has("peakWavelength")) {
        _peakWavelength =  json_val->getInt("peakWavelength");
    }
    return YSensor::_parseAttr(json_val);
}


/**
 * Retrieves the raw spectral intensity value as measured by the sensor, without any scaling or calibration.
 *
 * @return an integer
 *
 * On failure, throws an exception or returns YSpectralChannel::RAWCOUNT_INVALID.
 */
int YSpectralChannel::get_rawCount(void)
{
    int res = 0;
    yEnterCriticalSection(&_this_cs);
    try {
        if (_cacheExpiration <= YAPI::GetTickCount()) {
            if (this->_load_unsafe(YAPI::_yapiContext.GetCacheValidity()) != YAPI_SUCCESS) {
                {
                    yLeaveCriticalSection(&_this_cs);
                    return YSpectralChannel::RAWCOUNT_INVALID;
                }
            }
        }
        res = _rawCount;
    } catch (std::exception &) {
        yLeaveCriticalSection(&_this_cs);
        throw;
    }
    yLeaveCriticalSection(&_this_cs);
    return res;
}

/**
 * Returns the target spectral band name.
 *
 * @return a string corresponding to the target spectral band name
 *
 * On failure, throws an exception or returns YSpectralChannel::CHANNELNAME_INVALID.
 */
string YSpectralChannel::get_channelName(void)
{
    string res;
    yEnterCriticalSection(&_this_cs);
    try {
        if (_cacheExpiration <= YAPI::GetTickCount()) {
            if (this->_load_unsafe(YAPI::_yapiContext.GetCacheValidity()) != YAPI_SUCCESS) {
                {
                    yLeaveCriticalSection(&_this_cs);
                    return YSpectralChannel::CHANNELNAME_INVALID;
                }
            }
        }
        res = _channelName;
    } catch (std::exception &) {
        yLeaveCriticalSection(&_this_cs);
        throw;
    }
    yLeaveCriticalSection(&_this_cs);
    return res;
}

/**
 * Returns the target spectral band peak wavelenght, in nm.
 *
 * @return an integer corresponding to the target spectral band peak wavelenght, in nm
 *
 * On failure, throws an exception or returns YSpectralChannel::PEAKWAVELENGTH_INVALID.
 */
int YSpectralChannel::get_peakWavelength(void)
{
    int res = 0;
    yEnterCriticalSection(&_this_cs);
    try {
        if (_cacheExpiration <= YAPI::GetTickCount()) {
            if (this->_load_unsafe(YAPI::_yapiContext.GetCacheValidity()) != YAPI_SUCCESS) {
                {
                    yLeaveCriticalSection(&_this_cs);
                    return YSpectralChannel::PEAKWAVELENGTH_INVALID;
                }
            }
        }
        res = _peakWavelength;
    } catch (std::exception &) {
        yLeaveCriticalSection(&_this_cs);
        throw;
    }
    yLeaveCriticalSection(&_this_cs);
    return res;
}

/**
 * Retrieves a spectral analysis channel for a given identifier.
 * The identifier can be specified using several formats:
 *
 * - FunctionLogicalName
 * - ModuleSerialNumber.FunctionIdentifier
 * - ModuleSerialNumber.FunctionLogicalName
 * - ModuleLogicalName.FunctionIdentifier
 * - ModuleLogicalName.FunctionLogicalName
 *
 *
 * This function does not require that the spectral analysis channel is online at the time
 * it is invoked. The returned object is nevertheless valid.
 * Use the method isOnline() to test if the spectral analysis channel is
 * indeed online at a given time. In case of ambiguity when looking for
 * a spectral analysis channel by logical name, no error is notified: the first instance
 * found is returned. The search is performed first by hardware name,
 * then by logical name.
 *
 * If a call to this object's is_online() method returns FALSE although
 * you are certain that the matching device is plugged, make sure that you did
 * call registerHub() at application initialization time.
 *
 * @param func : a string that uniquely characterizes the spectral analysis channel, for instance
 *         MyDevice.spectralChannel1.
 *
 * @return a YSpectralChannel object allowing you to drive the spectral analysis channel.
 */
YSpectralChannel* YSpectralChannel::FindSpectralChannel(string func)
{
    YSpectralChannel* obj = NULL;
    int taken = 0;
    if (YAPI::_apiInitialized) {
        yEnterCriticalSection(&YAPI::_global_cs);
        taken = 1;
    }try {
        obj = (YSpectralChannel*) YFunction::_FindFromCache("SpectralChannel", func);
        if (obj == NULL) {
            obj = new YSpectralChannel(func);
            YFunction::_AddToCache("SpectralChannel", func, obj);
        }
    } catch (std::exception &) {
        if (taken) yLeaveCriticalSection(&YAPI::_global_cs);
        throw;
    }
    if (taken) yLeaveCriticalSection(&YAPI::_global_cs);
    return obj;
}

/**
 * Registers the callback function that is invoked on every change of advertised value.
 * The callback is invoked only during the execution of ySleep or yHandleEvents.
 * This provides control over the time when the callback is triggered. For good responsiveness, remember to call
 * one of these two functions periodically. To unregister a callback, pass a NULL pointer as argument.
 *
 * @param callback : the callback function to call, or a NULL pointer. The callback function should take two
 *         arguments: the function object of which the value has changed, and the character string describing
 *         the new advertised value.
 * @noreturn
 */
int YSpectralChannel::registerValueCallback(YSpectralChannelValueCallback callback)
{
    string val;
    if (callback != NULL) {
        YFunction::_UpdateValueCallbackList(this, true);
    } else {
        YFunction::_UpdateValueCallbackList(this, false);
    }
    _valueCallbackSpectralChannel = callback;
    // Immediately invoke value callback with current value
    if (callback != NULL && this->isOnline()) {
        val = _advertisedValue;
        if (!(val == "")) {
            this->_invokeValueCallback(val);
        }
    }
    return 0;
}

int YSpectralChannel::_invokeValueCallback(string value)
{
    if (_valueCallbackSpectralChannel != NULL) {
        _valueCallbackSpectralChannel(this, value);
    } else {
        YSensor::_invokeValueCallback(value);
    }
    return 0;
}

/**
 * Registers the callback function that is invoked on every periodic timed notification.
 * The callback is invoked only during the execution of ySleep or yHandleEvents.
 * This provides control over the time when the callback is triggered. For good responsiveness, remember to call
 * one of these two functions periodically. To unregister a callback, pass a NULL pointer as argument.
 *
 * @param callback : the callback function to call, or a NULL pointer. The callback function should take two
 *         arguments: the function object of which the value has changed, and an YMeasure object describing
 *         the new advertised value.
 * @noreturn
 */
int YSpectralChannel::registerTimedReportCallback(YSpectralChannelTimedReportCallback callback)
{
    YSensor* sensor = NULL;
    sensor = this;
    if (callback != NULL) {
        YFunction::_UpdateTimedReportCallbackList(sensor, true);
    } else {
        YFunction::_UpdateTimedReportCallbackList(sensor, false);
    }
    _timedReportCallbackSpectralChannel = callback;
    return 0;
}

int YSpectralChannel::_invokeTimedReportCallback(YMeasure value)
{
    if (_timedReportCallbackSpectralChannel != NULL) {
        _timedReportCallbackSpectralChannel(this, value);
    } else {
        YSensor::_invokeTimedReportCallback(value);
    }
    return 0;
}

YSpectralChannel *YSpectralChannel::nextSpectralChannel(void)
{
    string  hwid;

    if(YISERR(_nextFunction(hwid)) || hwid=="") {
        return NULL;
    }
    return YSpectralChannel::FindSpectralChannel(hwid);
}

YSpectralChannel *YSpectralChannel::FirstSpectralChannel(void)
{
    vector<YFUN_DESCR>   v_fundescr;
    YDEV_DESCR             ydevice;
    string              serial, funcId, funcName, funcVal, errmsg;

    if(YISERR(YapiWrapper::getFunctionsByClass("SpectralChannel", 0, v_fundescr, sizeof(YFUN_DESCR), errmsg)) ||
       v_fundescr.size() == 0 ||
       YISERR(YapiWrapper::getFunctionInfo(v_fundescr[0], ydevice, serial, funcId, funcName, funcVal, errmsg))) {
        return NULL;
    }
    return YSpectralChannel::FindSpectralChannel(serial+"."+funcId);
}

//--- (end of YSpectralChannel implementation)

//--- (YSpectralChannel functions)
//--- (end of YSpectralChannel functions)
