/*
htop - Eth1_StatsMeter.c
(C) 2020 @lex
*/

#include "Eth1_StatsMeter.h"
#include "Platform.h"
#include "CRT.h"

#include "interfaces.h"

/*{
#include "Meter.h"
}*/


int Eth1_StatsMeter_attributes[] = {
   ETH1_INTERFACE
};

/* borrowed from slurm */
static void Eth1_StatsMeter_setValues(Meter* this, char* buffer, int len) {
    int ret;
    float rxspeed;
    float txspeed;
    Settings* settings = this->pl->settings;
    double refreshdelay;
    static double old = 0.;
    static double now = 0.;
    static int flash = 0;

    now = get_wall_time();
    refreshdelay = now - old;
    if (old == 0.)
       refreshdelay = 1.;
    old = now;    

    if (settings->eth1_alias[0] != 0) {
        ret = Platform_getEth_stats(settings->eth1_alias, 1, 0);
    } else {
        ret = Platform_getEth_stats("eth1", 1, 0);
    }
    
    if (ret) {
        if (Platform_Eth1_stats.rx_bytes_comp > Platform_Eth1_stats.rx_bytes)
            Platform_Eth1_stats.rx_bytes_comp = 0;
        if (Platform_Eth1_stats.tx_bytes_comp > Platform_Eth1_stats.tx_bytes)
            Platform_Eth1_stats.tx_bytes_comp = 0;

        /* we assume here the refresdelay is 1 sec which sometimes is 1.5, so we have a peak */ 
        rxspeed = (Platform_Eth1_stats.rx_bytes - Platform_Eth1_stats.rx_bytes_comp) / refreshdelay;
        txspeed = (Platform_Eth1_stats.tx_bytes - Platform_Eth1_stats.tx_bytes_comp) / refreshdelay;

        xSnprintf(buffer, len, "%.2f KB/s - %.2f KB/s (TX/RX)", (float) txspeed / 1024, (float) rxspeed / 1024);

        Platform_Eth1_stats.rx_bytes_comp = Platform_Eth1_stats.rx_bytes;
        Platform_Eth1_stats.tx_bytes_comp = Platform_Eth1_stats.tx_bytes;

    } else {
        if (!(flash % 2))
            xSnprintf(buffer, len, "%s", "unavail");
        else
            xSnprintf(buffer, len, "%s", "");
        flash++;
    }
}

MeterClass Eth1_StatsMeter_class = {
   .super = {
      .extends = Class(Meter),
      .delete = Meter_delete
   },
   .updateValues = Eth1_StatsMeter_setValues, 
   .defaultMode = TEXT_METERMODE,
   .maxItems = 8,
   .total = 100.0,
   .attributes = Eth1_StatsMeter_attributes,
   .name = "Eth1stat",
   .uiName = "Eth1 stat",
   .caption = "Eth1 stat: ",
};


