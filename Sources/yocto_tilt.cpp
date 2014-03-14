/*********************************************************************
 *
 * $Id: yocto_tilt.cpp 15253 2014-03-06 10:15:50Z seb $
 *
 * Implements yFindTilt(), the high-level API for Tilt functions
 *
 * - - - - - - - - - License information: - - - - - - - - - 
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
#include "yocto_tilt.h"
#include "yapi/yjson.h"
#include "yapi/yapi.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

YTilt::YTilt(const string& func): YSensor(func)
//--- (Tilt initialization)
    ,_axis(AXIS_INVALID)
    ,_valueCallbackTilt(NULL)
    ,_timedReportCallbackTilt(NULL)
//--- (end of Tilt initialization)
{
    _className="Tilt";
}

YTilt::~YTilt() 
{
//--- (YTilt cleanup)
//--- (end of YTilt cleanup)
}
//--- (YTilt implementation)
// static attributes

int YTilt::_parseAttr(yJsonStateMachine& j)
{
    if(!strcmp(j.token, "axis")) {
        if(yJsonParse(&j) != YJSON_PARSE_AVAIL) goto failed;
        _axis =  (Y_AXIS_enum) atoi(j.token);
        return 1;
    }
    failed:
    return YSensor::_parseAttr(j);
}


Y_AXIS_enum YTilt::get_axis(void)
{
    if (_cacheExpiration <= YAPI::GetTickCount()) {
        if (this->load(YAPI::DefaultCacheValidity) != YAPI_SUCCESS) {
            return YTilt::AXIS_INVALID;
        }
    }
    return _axis;
}

/**
 * Retrieves $AFUNCTION$ for a given identifier.
 * The identifier can be specified using several formats:
 * <ul>
 * <li>FunctionLogicalName</li>
 * <li>ModuleSerialNumber.FunctionIdentifier</li>
 * <li>ModuleSerialNumber.FunctionLogicalName</li>
 * <li>ModuleLogicalName.FunctionIdentifier</li>
 * <li>ModuleLogicalName.FunctionLogicalName</li>
 * </ul>
 * 
 * This function does not require that $THEFUNCTION$ is online at the time
 * it is invoked. The returned object is nevertheless valid.
 * Use the method YTilt.isOnline() to test if $THEFUNCTION$ is
 * indeed online at a given time. In case of ambiguity when looking for
 * $AFUNCTION$ by logical name, no error is notified: the first instance
 * found is returned. The search is performed first by hardware name,
 * then by logical name.
 * 
 * @param func : a string that uniquely characterizes $THEFUNCTION$
 * 
 * @return a YTilt object allowing you to drive $THEFUNCTION$.
 */
YTilt* YTilt::FindTilt(string func)
{
    YTilt* obj = NULL;
    obj = (YTilt*) YFunction::_FindFromCache("Tilt", func);
    if (obj == NULL) {
        obj = new YTilt(func);
        YFunction::_AddToCache("Tilt", func, obj);
    }
    return obj;
}

/**
 * Registers the callback function that is invoked on every change of advertised value.
 * The callback is invoked only during the execution of ySleep or yHandleEvents.
 * This provides control over the time when the callback is triggered. For good responsiveness, remember to call
 * one of these two functions periodically. To unregister a callback, pass a null pointer as argument.
 * 
 * @param callback : the callback function to call, or a null pointer. The callback function should take two
 *         arguments: the function object of which the value has changed, and the character string describing
 *         the new advertised value.
 * @noreturn
 */
int YTilt::registerValueCallback(YTiltValueCallback callback)
{
    string val;
    if (callback != NULL) {
        YFunction::_UpdateValueCallbackList(this, true);
    } else {
        YFunction::_UpdateValueCallbackList(this, false);
    }
    _valueCallbackTilt = callback;
    // Immediately invoke value callback with current value
    if (callback != NULL && this->isOnline()) {
        val = _advertisedValue;
        if (!(val == "")) {
            this->_invokeValueCallback(val);
        }
    }
    return 0;
}

int YTilt::_invokeValueCallback(string value)
{
    if (_valueCallbackTilt != NULL) {
        _valueCallbackTilt(this, value);
    } else {
        YSensor::_invokeValueCallback(value);
    }
    return 0;
}

/**
 * Registers the callback function that is invoked on every periodic timed notification.
 * The callback is invoked only during the execution of ySleep or yHandleEvents.
 * This provides control over the time when the callback is triggered. For good responsiveness, remember to call
 * one of these two functions periodically. To unregister a callback, pass a null pointer as argument.
 * 
 * @param callback : the callback function to call, or a null pointer. The callback function should take two
 *         arguments: the function object of which the value has changed, and an YMeasure object describing
 *         the new advertised value.
 * @noreturn
 */
int YTilt::registerTimedReportCallback(YTiltTimedReportCallback callback)
{
    if (callback != NULL) {
        YFunction::_UpdateTimedReportCallbackList(this, true);
    } else {
        YFunction::_UpdateTimedReportCallbackList(this, false);
    }
    _timedReportCallbackTilt = callback;
    return 0;
}

int YTilt::_invokeTimedReportCallback(YMeasure value)
{
    if (_timedReportCallbackTilt != NULL) {
        _timedReportCallbackTilt(this, value);
    } else {
        YSensor::_invokeTimedReportCallback(value);
    }
    return 0;
}

YTilt *YTilt::nextTilt(void)
{
    string  hwid;
    
    if(YISERR(_nextFunction(hwid)) || hwid=="") {
        return NULL;
    }
    return YTilt::FindTilt(hwid);
}

YTilt* YTilt::FirstTilt(void)
{
    vector<YFUN_DESCR>   v_fundescr;
    YDEV_DESCR             ydevice;
    string              serial, funcId, funcName, funcVal, errmsg;
    
    if(YISERR(YapiWrapper::getFunctionsByClass("Tilt", 0, v_fundescr, sizeof(YFUN_DESCR), errmsg)) ||
       v_fundescr.size() == 0 ||
       YISERR(YapiWrapper::getFunctionInfo(v_fundescr[0], ydevice, serial, funcId, funcName, funcVal, errmsg))) {
        return NULL;
    }
    return YTilt::FindTilt(serial+"."+funcId);
}

//--- (end of YTilt implementation)

//--- (Tilt functions)
//--- (end of Tilt functions)
