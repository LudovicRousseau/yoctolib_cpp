#include "yocto_api.h"
#include "yocto_digitalio.h"
#include <iostream>
#include <ctype.h>
#include <stdlib.h>

using namespace std;

static void usage(void)
{
    cout << "usage: demo <serial_number>  " << endl;
    cout << "       demo <logical_name> " << endl;
    cout << "       demo any           (use any discovered device)" << endl;
    u64 now = yGetTickCount();
	while (yGetTickCount()-now<3000) {
        // wait 3 sec to show the message
    }
    exit(1);
}

int main(int argc, const char * argv[])
{
    string  errmsg;
    string  target;
    YDigitalIO  *io;
    
    if (argc < 2) {
        usage();
    }
    target = (string) argv[1];
   
    // Setup the API to use local USB devices
    if (yRegisterHub("usb", errmsg) != YAPI_SUCCESS) {
        cerr << "RegisterHub error: " << errmsg << endl;
        return 1;
    }
    
    if (target == "any") {
        // try to find the first available digitial IO  feature
        io =  yFirstDigitalIO();        
        if (io==NULL) {
            cout << "No module connected (check USB cable)" << endl;
            return 1;
        }
    }else{
        io =  yFindDigitalIO(target + ".digitalIO");
    }


    // make sure the device is here
    if (!io->isOnline()) {
        cout << "Module not connected (check identification and USB cable)" << endl;
        return 1;
    }

    // lets configure the channels direction
    // bits 0..3 as output
    // bits 4..7 as input
  
    io->set_portDirection(0x0F);
    io->set_portPolarity(0); // polarity set to regular
    io->set_portOpenDrain(0); // No open drain

    cout <<"Channels 0..3 are configured as outputs and channels 4..7" << endl;
    cout <<"are configred as inputs, you can connect some inputs to" << endl;
    cout <<"ouputs and see what happens" << endl;
   
    int  outputdata = 0;
    while (io->isOnline()) {
       outputdata = (outputdata +1) % 16; // cycle ouput 0..15
       io->set_portState(outputdata); // We could have used set_bitState as well
       ySleep(1000,errmsg);
       int inputdata = io->get_portState(); // read port values
       string line="";  // display port value as binary
       for (int i = 0; i<8 ; i++) {
            if  (inputdata & (128 >> i))  
                line=line+'1'; 
            else 
                line=line+'0';
       }
       cout << "port value = " << line << endl;
    }
    cout << "Module disconnected" << endl;
}
