NAME := webview_cli

PATH_RELEASE := $(NAME)
PATH_DEBUG := $(NAME)_debug

PLATFORM_RELEASE = $(PATH_RELEASE)
PLATFORM_DEBUG = $(PATH_DEBUG)

PATH_WEBVIEW = ./webview.h

CC = g++ --std=c++11 main.cpp
CC_RELEASE = $(CC) -O3
CC_DEBUG = $(CC) -g -Wall -pedantic -DDEBUG
CC_FILES = $(PATH_WEBVIEW) main.cpp

MAC_APP := $(NAME).app
MAC_PLIST = $(MAC_APP)/Info.plist
MAC_FLAGS = -framework WebKit
MAC_PATH = $(MAC_APP)/$(NAME)

LINUX_FLAGS = $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0)



ifeq ($(shell uname -s), Darwin)
PLATFORM_RELEASE = $(MAC_PATH)
PLATFORM_FLAGS = $(MAC_FLAGS)
endif



release: ${PLATFORM_RELEASE}

debug: ${PLATFORM_DEBUG}



${PATH_WEBVIEW}:
	curl -sSLo $(PATH_WEBVIEW) "https://raw.githubusercontent.com/webview/webview/master/webview.h"

${PATH_DEBUG}: $(CC_FILES)
	${CC_DEBUG} ${PLATFORM_FLAGS} -o ${PATH_DEBUG}

${PATH_RELEASE}: $(CC_FILES)
	${CC_RELEASE} ${PLATFORM_FLAGS} -o ${PATH_RELEASE}

${MAC_PLIST}:
	mkdir $(MAC_APP)
	echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" >> $(MAC_PLIST)
	echo "<plist version=\"1.0\">" >> $(MAC_PLIST)
	echo "<dict>" >> $(MAC_PLIST)
	echo "	<key>CFBundleExecutable</key>" >> $(MAC_PLIST)
	echo "	<string>${NAME}</string>" >> $(MAC_PLIST)
	echo "	<key>NSHighResolutionCapable</key>" >> $(MAC_PLIST)
	echo "	<true/>" >> $(MAC_PLIST)
	echo "</dict>" >> $(MAC_PLIST)
	echo "</plist>" >> $(MAC_PLIST)

${MAC_PATH}: $(CC_FILES) $(MAC_PLIST)
	$(CC_RELEASE) $(MAC_FLAGS) -o $(MAC_PATH)
