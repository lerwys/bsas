#!../../bin/linux-x86_64/dspam

dbLoadDatabase("../../dbd/dspam.dbd",0,0)
dspam_registerRecordDeviceDriver(pdbbase)

spammerCreate("spam", "224.0.0.128", "127.0.0.1")
dbLoadRecords("../../db/dspamCounterTable100.db", "P=cntTblA,NAME=spam")

iocInit()
