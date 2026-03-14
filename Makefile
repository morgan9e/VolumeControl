# VolumeControl Makefile

CXX = clang++
CC = clang
CXXFLAGS = -std=c++11 -stdlib=libc++ -fPIC -O2
OBJCFLAGS = -fobjc-arc
CFLAGS =

BUILD_DIR = build

DRIVER_INSTALL_PATH = /Library/Audio/Plug-Ins/HAL
APP_INSTALL_PATH = /Applications
LAUNCHAGENT_DIR = $(HOME)/Library/LaunchAgents
LAUNCHAGENT_PLIST = com.volumecontrol.App.plist

# --- Driver ---

DRIVER_NAME = VolumeControl
DRIVER_BUNDLE = $(BUILD_DIR)/$(DRIVER_NAME).driver
DRIVER_CONTENTS = $(DRIVER_BUNDLE)/Contents
DRIVER_MACOS = $(DRIVER_CONTENTS)/MacOS
DRIVER_BINARY = $(DRIVER_MACOS)/$(DRIVER_NAME)

DRIVER_SRC = driver
DRIVER_UTIL = driver/PublicUtility
SHARED = shared

DRIVER_INCLUDES = -I$(DRIVER_SRC) -I$(DRIVER_UTIL) -I$(SHARED)
DRIVER_FRAMEWORKS = -framework CoreAudio -framework CoreFoundation -framework IOKit -framework Accelerate

DRIVER_CPP_SRCS = $(wildcard $(DRIVER_SRC)/*.cpp) $(wildcard $(DRIVER_UTIL)/*.cpp) $(wildcard $(SHARED)/*.cpp)
DRIVER_CPP_OBJS = $(patsubst %.cpp,$(BUILD_DIR)/driver_obj/%.o,$(DRIVER_CPP_SRCS))

# --- Daemon ---

APP_NAME = VolumeControl
APP_BUNDLE = $(BUILD_DIR)/$(APP_NAME).app
APP_CONTENTS = $(APP_BUNDLE)/Contents
APP_MACOS = $(APP_CONTENTS)/MacOS
APP_BINARY = $(APP_MACOS)/$(APP_NAME)

APP_SRC = app
APP_UTIL = app/PublicUtility

APP_INCLUDES = -I$(APP_SRC) -I$(APP_UTIL) -I$(SHARED)
APP_FRAMEWORKS = -framework Cocoa -framework CoreAudio -framework AudioToolbox -framework Accelerate -framework AVFoundation

APP_CPP_SRCS = $(wildcard $(APP_SRC)/*.cpp) $(wildcard $(APP_UTIL)/*.cpp) $(wildcard $(SHARED)/*.cpp)
APP_MM_SRCS = $(wildcard $(APP_SRC)/*.mm)
APP_C_SRCS = $(wildcard $(APP_UTIL)/*.c)

APP_CPP_OBJS = $(patsubst %.cpp,$(BUILD_DIR)/app_obj/%.o,$(APP_CPP_SRCS))
APP_MM_OBJS = $(patsubst %.mm,$(BUILD_DIR)/app_obj/%.o,$(APP_MM_SRCS))
APP_C_OBJS = $(patsubst %.c,$(BUILD_DIR)/app_obj/%.o,$(APP_C_SRCS))

APP_ALL_OBJS = $(APP_CPP_OBJS) $(APP_MM_OBJS) $(APP_C_OBJS)

# --- Targets ---

.PHONY: all driver daemon install uninstall clean

all: driver daemon

driver: $(DRIVER_BINARY) $(DRIVER_CONTENTS)/Info.plist

$(DRIVER_BINARY): $(DRIVER_CPP_OBJS)
	@mkdir -p $(DRIVER_MACOS)
	$(CXX) $(CXXFLAGS) $(DRIVER_FRAMEWORKS) -bundle -o $@ $^

$(DRIVER_CONTENTS)/Info.plist: $(DRIVER_SRC)/Info.plist
	@mkdir -p $(DRIVER_CONTENTS)
	cp $< $@

$(BUILD_DIR)/driver_obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DRIVER_INCLUDES) -c -o $@ $<

daemon: $(APP_BINARY) $(APP_CONTENTS)/Info.plist

$(APP_BINARY): $(APP_ALL_OBJS)
	@mkdir -p $(APP_MACOS)
	$(CXX) $(CXXFLAGS) $(OBJCFLAGS) $(APP_FRAMEWORKS) -o $@ $^

$(APP_CONTENTS)/Info.plist: $(APP_SRC)/Info.plist
	@mkdir -p $(APP_CONTENTS)
	cp $< $@

$(BUILD_DIR)/app_obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(APP_INCLUDES) -c -o $@ $<

$(BUILD_DIR)/app_obj/%.o: %.mm
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJCFLAGS) $(APP_INCLUDES) -c -o $@ $<

$(BUILD_DIR)/app_obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_INCLUDES) -c -o $@ $<

install-app: daemon
	@echo "Installing VolumeControl app only (no driver change)..."
	@launchctl unload "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)" 2>/dev/null; true
	@killall VolumeControl 2>/dev/null; sleep 1; true
	sudo rm -rf "$(APP_INSTALL_PATH)/$(APP_NAME).app"
	sudo cp -R "$(APP_BUNDLE)" "$(APP_INSTALL_PATH)/"
	sudo xattr -cr "$(APP_INSTALL_PATH)/$(APP_NAME).app"
	launchctl load "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)" 2>/dev/null; true
	@echo "Done."

install: all
	@echo "Installing VolumeControl..."
	@# Stop any running instance before touching the driver.
	@launchctl unload "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)" 2>/dev/null; true
	@killall VolumeControl 2>/dev/null; sleep 1; true
	sudo mkdir -p "$(DRIVER_INSTALL_PATH)"
	sudo rm -rf "$(DRIVER_INSTALL_PATH)/$(DRIVER_NAME).driver"
	sudo cp -R "$(DRIVER_BUNDLE)" "$(DRIVER_INSTALL_PATH)/"
	sudo xattr -cr "$(DRIVER_INSTALL_PATH)/$(DRIVER_NAME).driver"
	sudo killall -9 coreaudiod
	@echo "Waiting for coreaudiod..."
	@sleep 5
	sudo mkdir -p "$(APP_INSTALL_PATH)"
	sudo rm -rf "$(APP_INSTALL_PATH)/$(APP_NAME).app"
	sudo cp -R "$(APP_BUNDLE)" "$(APP_INSTALL_PATH)/"
	sudo xattr -cr "$(APP_INSTALL_PATH)/$(APP_NAME).app"
	@# Install LaunchAgent to start on login
	@mkdir -p "$(LAUNCHAGENT_DIR)"
	@echo '<?xml version="1.0" encoding="UTF-8"?>' > "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '<plist version="1.0"><dict>' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '  <key>Label</key><string>com.volumecontrol.App</string>' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '  <key>ProgramArguments</key><array><string>/Applications/VolumeControl.app/Contents/MacOS/VolumeControl</string></array>' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '  <key>RunAtLoad</key><true/>' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '  <key>KeepAlive</key><true/>' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@echo '</dict></plist>' >> "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	launchctl load "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)" 2>/dev/null; true
	@echo "Done. VolumeControl daemon is running and will start on login."

uninstall:
	@echo "Uninstalling VolumeControl..."
	@launchctl unload "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)" 2>/dev/null; true
	@rm -f "$(LAUNCHAGENT_DIR)/$(LAUNCHAGENT_PLIST)"
	@killall VolumeControl 2>/dev/null; sleep 1; true
	sudo rm -rf "$(DRIVER_INSTALL_PATH)/$(DRIVER_NAME).driver"
	sudo rm -rf "$(APP_INSTALL_PATH)/$(APP_NAME).app"
	sudo killall -9 coreaudiod
	@defaults delete com.volumecontrol.App 2>/dev/null; true
	@echo "Done."

clean:
	rm -rf $(BUILD_DIR)
