#include "yocto_api.h"
#include "yocto_current.h"
#include <iostream>
#include <stdlib.h>

using namespace std;

static void usage(void)
{
    cout << "usage: demo <serial_number> " << endl;
    cout << "       demo <logical_name>" << endl;
    cout << "       demo any                 (use any discovered device)" << endl;
    u64 now = yGetTickCount();
	while (yGetTickCount()-now<3000) {
        // wait 3 sec to show the message
    }
    exit(1);
}

int main(int argc, const char * argv[])
{
    string       errmsg;
    string       target;
    YCurrent    *sensor;
    YCurrent    *sensorAC;
    YCurrent    *sensorDC;
    YModule     *m;

    if (argc < 2)  usage();
    
    target = (string) argv[1];

    YAPI::DisableExceptions();
    
    // Setup the API to use local USB devices
    if (YAPI::RegisterHub("usb", errmsg) != YAPI_SUCCESS) {
        cerr << "RegisterHub error: " << errmsg << endl;
        return 1;
    }

    if (target == "any") {
        // retreive any voltage sensor (can be AC or DC)
        sensor = YCurrent::FirstCurrent();
        if (sensor==NULL) {
            cerr <<"No module connected (Check cable)"<< endl;
            exit(1);
        }
    } else {
        sensor = YCurrent::FindCurrent(target + ".current1");
    }
    
    // we need to retreive both DC and AC voltage from the device.    
    if (sensor->isOnline())  {
        m = sensor->get_module();
        sensorDC = YCurrent::FindCurrent(m->get_serialNumber() + ".current1");
        sensorAC = YCurrent::FindCurrent(m->get_serialNumber() + ".current2");
    } else {
        cerr <<"No module connected (Check cable)"<< endl;
        exit(1);
    }
    while(1) {
        if (!sensorDC->isOnline())  {
            cout << "Module disconnected" << endl;        
            break;
        }
        cout << "Current,  DC : " << sensorDC->get_currentValue() << " mA";
        cout << "   AC : " << sensorAC->get_currentValue() << " mA";
        cout << "  (press Ctrl-C to exit)" << endl;
        YAPI::Sleep(1000,errmsg);
    };
        
    return 0;
}
