#!../../bin/linux-x86_64/testServer

## Register all support components
dbLoadDatabase("../../dbd/testServer.dbd",0,0)
testServer_registerRecordDeviceDriver(pdbbase)

## Load record instances
dbLoadRecords("../../db/testServer.db")

# Add some scalar PVs
testServerPVScalarAdd("TEST:PV1")
testServerPVScalarAdd("TEST:PV2")
testServerPVScalarAdd("TEST:PV3")
testServerPVScalarAdd("TEST:PV4")

# Set server PV update rate
testServerPVUpdatePeriod(1.0)

iocInit()
