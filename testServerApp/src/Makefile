TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

PROD_SRCS += pvaTestServer.cpp

PROD_IOC = testServer
# testServer.dbd will be created and installed
DBD += testServer.dbd

# testServer.dbd will be made up from these files:
testServer_DBD += base.dbd
testServer_DBD += system.dbd
testServer_DBD += PVAServerRegister.dbd
testServer_DBD += qsrv.dbd
testServer_DBD += testServerSupport.dbd

# Add all the support libraries needed by this IOC
#testServer_LIBS += xxx

testServer_SRCS += testServerMain.cpp
# testServer_registerRecordDeviceDriver.cpp derives from testServer.dbd
testServer_SRCS += testServer_registerRecordDeviceDriver.cpp
testServer_SRCS += hooks.cpp

# Build the main IOC entry point on workstation OSs.
#testServer_SRCS_DEFAULT += testServerMain.cpp

# Finally link to the EPICS Base libraries
PROD_LIBS += qsrv
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

