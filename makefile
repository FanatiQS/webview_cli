main: webview.h
	g++ --std=c++11 main.cpp -o ./webview -framework WebKit -O3

debug:
	g++ --std=c++11 main.cpp -o ./webview -framework WebKit -g -Wall -pedantic -DDEBUG

webview.h:
	curl -sSLo "./webview.h" "https://raw.githubusercontent.com/webview/webview/master/webview.h"
