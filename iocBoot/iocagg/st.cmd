#!../../bin/linux-x86_64/agg

< envPaths

## Register all support components
dbLoadDatabase("${TOP}/dbd/agg.dbd",0,0)
agg_registerRecordDeviceDriver(pdbbase)

var(controllerNumWorkQueue, 4)

var(collectorDebug, 3)
var(collectorPvaDebug, 3)

aggTableAdd("RX:")

iocInit()
