#!../../bin/linux-x86_64/agg

< envPaths

## Register all support components
dbLoadDatabase("${TOP}/dbd/agg.dbd",0,0)
agg_registerRecordDeviceDriver(pdbbase)

var(controllerNumWorkQueue, 4)

var(collectorDebug, 1)
var(collectorPvaDebug, 1)

var(aggregatorPvaDebug, 1)

aggTableAdd("RX:")

iocInit()
