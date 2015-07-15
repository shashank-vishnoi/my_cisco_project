TOPDIR := ..

include $(TOPDIR)/config.mk

ifeq ($(PLATFORM_NAME_IS_IC), 1)
# prepend RDK_INC on INCLUDE_PATHS
INC_TEMP := $(INCLUDE_PATHS)
endif

ifeq ($(PLATFORM_NAME_IS_G6), 1)
	SRCS := shim-psiUtils-cperpv1.cpp shim-dfb-cperpv1.cpp shim-video-scaling-cperpv1.cpp shim-mrdvr-max-client-cperpv1.cpp shim-AppDataUtils-cperpv1.cpp shim-current-video-cperpv1.cpp \
                shim-mrdvr-finalize-cperpv1.cpp
endif

ifeq ($(PLATFORM_NAME_IS_G8), 1)
	SRCS := shim-psiUtils-cperpv2.cpp shim-dfb-cperpv2.cpp shim-video-scaling-cperpv2.cpp shim-mrdvr-max-client-cperpv2.cpp shim-AppDataUtils-cperpv2.cpp shim-current-video-cperpv2.cpp shim-mrdvr-finalize-cperpv2.cpp MSPMediaShrink.cpp
endif
ifeq ($(PLATFORM_NAME_IS_IC), 1)
	SRCS := shim-mrdvr-max-client-cperpv2.cpp shim-AppDataUtils-cperpv2.cpp
endif 

ifeq ($(PLATFORM_NAME_IS_G6_OR_G8), 1)
SRCS += zapper.cpp dvr.cpp DisplaySession.cpp RecordSession.cpp MediaPlayer.cpp IMediaPlayer.cpp TsbHandler.cpp IMediaStreamer.cpp IMediaPlayerSession.cpp \
    languageSelection.cpp psi.cpp pmt.cpp avpm.cpp avpm_VOD1080p.cpp eventQueue.cpp UnifiedSetting.cpp IPlaySession.cpp MSPEventCallback.cpp \
    MSPSource.cpp MSPRFSource.cpp MSPFileSource.cpp MSPPPVSource.cpp  MSPSourceFactory.cpp MSPResMonClient.cpp\
    OnDemandSystem.cpp MspCommon.cpp dsmccProtocol.cpp lscProtocolclass.cpp VOD_StreamControl.cpp SeaChange_StreamControl.cpp \
    VOD_SessionControl.cpp SeaChange_SessionControl.cpp ondemand.cpp mrdvr.cpp MSPHTTPSource.cpp mrdvrserver.cpp \
    ApplicationData.cpp ApplicationDataExt.cpp MusicAppData.cpp dvr_metadata_reader.cpp AnalogPsi.cpp MediaControllerClassFactory.cpp audioPlayer.cpp \
    MSPBase64.cpp MspMpEventMgr.cpp VODFactory.cpp Arris_SessionControl.cpp Arris_StreamControl.cpp CiscoCakSessionHandler.cpp \
    MSPMrdvrStreamerSource.cpp MrdvrRecStreamer.cpp CloudDvr_SessionControl.cpp CloudDvr_StreamControl.cpp 
endif
ifeq ($(PLATFORM_NAME_IS_G8), 1)
SRCS +=  HnOnDemandStreamer.cpp MSPHnOnDemandStreamerSource.cpp MrdvrTsbStreamer.cpp
endif

#MediaRTT_ic.cpp is specifically made as a seperate line as it can be removed easily when the Full AKE problem from RDK is solved.
ifeq ($(PLATFORM_NAME_IS_IC), 1)
 SRCS += MediaPlayerSseEventHandler.cpp zapper_ic.cpp MediaPlayer.cpp IMediaPlayer.cpp IMediaPlayerSession.cpp \
    languageSelection.cpp avpm_ic.cpp eventQueue.cpp UnifiedSetting.cpp IPlaySession.cpp MSPEventCallback.cpp \
    MSPSource.cpp MSPHTTPSource_ic.cpp MSPPPVSource_ic.cpp MSPSourceFactory.cpp MSPResMonClient.cpp \
    psi_ic.cpp pmt_ic.cpp MspCommon.cpp dsmccProtocol.cpp lscProtocolclass.cpp mrdvr_ic.cpp \
    MediaRTT_ic.cpp \
    ApplicationData.cpp ApplicationDataExt_ic.cpp MusicAppData.cpp MediaControllerClassFactory.cpp audioPlayer_ic.cpp \
    MSPBase64.cpp MspMpEventMgr.cpp CiscoCakSessionHandler.cpp csci-ipclient-msp-api.cpp \
    vodUtils.cpp
endif

ifeq ($(PLATFORM_NAME_IS_G6), 1)
SRCS += shim-1394-cperpv1.cpp
endif
ifeq ($(PLATFORM_NAME_IS_G8_OR_IC), 1)
#TODO: shim-1394-cperpv2.cpp contains stub code and does nothing.
# G8 or IP-Client does not have 1394 port therefore this files 
# should be removed
SRCS += shim-1394-cperpv2.cpp
endif



OBJS := $(SRCS:%.cpp=$(PLATFORM_OBJ_DIR)/%.o)

TARGET := ../$(PLATFORM_LIB_PATH)/libmediaplayer.a

ZAPPER_TEST_TARGET := ./zapper_test
MEDIA_PLAYER_TEST_TARGET := ./MediaPlayer_test
DISPLAY_TEST_TARGET := ./display_test
LANGUAGE_SELECTION_TEST_TARGET := ./language_selection_test
AVPM_TEST_TARGET := ./avpm_test
PSI_TEST_TARGET := ./psi_test
TEST_TARGET := ./test

#Adding the flag RTT_TIMER_RETRY to the compilation so that removing this flag will remove the RTT code from compilation easily.
CPPFLAGS += -fno-strict-aliasing
ifeq ($(PLATFORM_NAME_IS_IC), 1)
CPPFLAGS += -Wno-ignored-qualifiers -DRTT_TIMER_RETRY
endif



.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) r $@ $(OBJS)

$(PLATFORM_OBJ_DIR)/CloudDvr_StreamControl.o: CloudDvr_StreamControl.h

$(MEDIA_PLAYER_TEST_TARGET): $(OBJS) MediaPlayer_test.h 
	echo "making Media Player Test target"
	../cxxtest/cxxtestgen.py --error-printer -o MediaPlayer_test.cpp MediaPlayer_test.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -I../cxxtest/ -o MediaPlayer_test.o MediaPlayer_test.cpp
	$(CC) -L../../antgalio/bin $(LDFLAGS) -o MediaPlayer_test  MediaPlayer_test.o -Wl,--start-group ../$(PLATFORM_LIB_PATH)/libavpm.a  ../$(PLATFORM_LIB_PATH)/libmediaplayer.a $(LIBS) $(UI_LIBS) -Wl,--end-group

$(LANGUAGE_SELECTION_TEST_TARGET): $(OBJS) language_selection_test.h
	echo "Making language Selection Test target"
	../cxxtest/cxxtestgen.py --error-printer -o language_selection_test.cpp language_selection_test.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -I../cxxtest/ -o language_selection_test.o language_selection_test.cpp
	$(CC) $(LDFLAGS) -o language_selection_test  language_selection_test.o languageSelection.o psi.o  UnifiedSetting.o avpm.o eventQueue.o ../$(PLATFORM_LIB_PATH)/libcnl.a	../nps/lib_$(PLATFORM)/libdb.a

$(ZAPPER_TEST_TARGET): $(OBJS) zapper_test.h
	echo "making zapper target"
	../cxxtest/cxxtestgen.py --error-printer -o zapper_test.cpp zapper_test.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -I../cxxtest/ -o zapper_test.o zapper_test.cpp
	$(CC) $(LDFLAGS) -o zapper_test zapper_test.o zapper.o DisplaySession.o   languageSelection.o psi.o avpm.o eventQueue.o UnifiedSetting.o Cam.o IPlaySession.o MSPSourceFactory.o MSPRFSource.o \
	MSPSource.o MSPFileSource.o MSPPPVSource.o -Wl,--start-group ../$(PLATFORM_LIB_PATH)/libsam.a ../$(PLATFORM_LIB_PATH)/libcnl.a ../$(PLATFORM_LIB_PATH)/libclm.a ../nps/lib_$(PLATFORM)/libdb.a -Wl,--end-group

$(DISPLAY_TEST_TARGET): $(OBJS) display_test.h
	echo "making display target"
	../cxxtest/cxxtestgen.py --error-printer -o display_test.cpp display_test.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -I../cxxtest/ -o display_test.o display_test.cpp
	$(CC) $(LDFLAGS) -o display_test display_test.o DisplaySession.o languageSelection.o psi.o avpm.o eventQueue.o UnifiedSetting.o Cam.o IPlaySession.o MSPSourceFactory.o MSPRFSource.o \
	MSPSource.o MSPFileSource.o MSPPPVSource.o ../$(PLATFORM_LIB_PATH)/libcnl.a ../$(PLATFORM_LIB_PATH)/libclm.a ../nps/lib_$(PLATFORM)/libdb.a

$(AVPM_TEST_TARGET): $(OBJS) avpm_test.h
	echo "making avpm target"
	../cxxtest/cxxtestgen.py --error-printer -o avpm_test.cpp avpm_test.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -I../cxxtest/ -o avpm_test.o avpm_test.cpp
	$(CC) $(LDFLAGS) -o avpm_test avpm_test.o DisplaySession.o languageSelection.o psi.o avpm.o eventQueue.o Cam.o IPlaySession.o UnifiedSetting.o \
	../$(PLATFORM_LIB_PATH)/libcnl.a ../$(PLATFORM_LIB_PATH)/libclm.a ../nps/lib_$(PLATFORM)/libdb.a

$(PSI_TEST_TARGET): $(OBJS) psi_test.h
	echo "making psi target"
	../cxxtest/cxxtestgen.py --error-printer -o psi_test.cpp psi_test.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -I../cxxtest/ -o psi_test.o psi_test.cpp
	$(CC) $(LDFLAGS) -o psi_test psi_test.o psi.o eventQueue.o  \
	../$(PLATFORM_LIB_PATH)/libcnl.a ../$(PLATFORM_LIB_PATH)/libclm.a ../nps/lib_$(PLATFORM)/libdb.a

$(TEST_TARGET): $(OBJS)
	echo "making test target"
	$(CC) $(LDFLAGS) -o test test.o eventQueue.o

clean:
	rm -f $(OBJS) $(TARGET) $(ZAPPER_TEST_TARGET) $(MEDIA_PLAYER_TEST_TARGET) $(LANGUAGE_SELECTION_TEST_TARGET)$(PSI_TEST_TARGET) $(AVPM_TEST_TARGET) $(DISPLAY_TEST_TARGET)
	$(DELETE_OBJ_DIR)



