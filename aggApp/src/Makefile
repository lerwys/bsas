TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = agg
# agg.dbd will be created and installed
DBD += agg.dbd

# agg.dbd will be made up from these files:
agg_DBD += base.dbd

# Include dbd files from all support applications:
agg_DBD += system.dbd
agg_DBD += PVAServerRegister.dbd
agg_DBD += qsrv.dbd
agg_DBD += aggSupport.dbd

# Add all the support libraries needed by this IOC
#agg_LIBS += xxx

# agg_registerRecordDeviceDriver.cpp derives from agg.dbd
agg_SRCS += agg_registerRecordDeviceDriver.cpp
agg_SRCS += hooks.cpp
agg_SRCS += controller.cpp
agg_SRCS += collector.cpp
agg_SRCS += collect_pva.cpp

# Build the main IOC entry point on workstation OSs.
agg_SRCS_DEFAULT += aggMain.cpp
agg_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#agg_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
agg_LIBS += qsrv
agg_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
agg_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

