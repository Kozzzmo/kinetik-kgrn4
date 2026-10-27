#!/usr/bin/make -f
# Kinetik KGRN-4 — build with DPF (https://github.com/DISTRHO/DPF)
NAME = KinetikKGRN4
FILES_DSP = KgrnPlugin.cpp third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
FILES_UI  = KgrnUI.cpp FileDrop.cpp Updater.cpp
ifeq ($(shell uname -s),Darwin)
FILES_UI += FileDropMac.mm
endif
DPF_PATH ?= ../dpf
DPF_TARGET_DIR = ../bin
DPF_BUILD_DIR = ../build
include $(DPF_PATH)/Makefile.plugins.mk
BUILD_CXX_FLAGS += -std=gnu++17 -DKN_VERSION='"$(shell cat ../VERSION)"'
KN_VPARTS := $(subst ., ,$(shell cat ../VERSION))
BUILD_CXX_FLAGS += -DKN_VER_MAJOR=$(or $(word 1,$(KN_VPARTS)),0) -DKN_VER_MINOR=$(or $(word 2,$(KN_VPARTS)),0) -DKN_VER_MICRO=$(or $(word 3,$(KN_VPARTS)),0)
ifeq ($(WINDOWS),true)
LINK_FLAGS += -lole32 -lshell32 -luuid
endif
TARGETS = vst3 clap
ifeq ($(MACOS),true)
TARGETS += au
endif
ifeq ($(LINUX),true)
TARGETS += jack
endif
all: $(TARGETS)
