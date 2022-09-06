#include <stdlib.h> // STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, system, NULL, malloc, exit, EXIT_FAILURE, EXIT_SUCCESS, atoi
#include <stdio.h> // printf

#include <unistd.h> // write, close, read, pipe, fork, dup2, chdir, pid_t
#include <libgen.h> // dirname

#include <thread> // std::thread
#include <fstream>
#include <iostream>
#include <sstream>

#include "./webview.h"



#define JS_OS "macos_x86_64"



// Only enable debug prints if DEBUG macro is defined
#ifdef DEBUG
#define DEBUG_PRINTF(...) do {printf(__VA_ARGS__);} while (0)
#else
#define DEBUG_PRINTF(...) do {} while (0)
#endif



// Evaluates javascript code on main thread to not cause crashes
void js_evalOnMain(webview_t w, void* arg) {
	webview_eval(w, (const char*)arg);
}

// Rejects javascript promise with error message
#define JS_REJECT(w, seq, msg) webview_return(w, seq, 1, "\"" msg "\"")

// Size of pipe read buffer
#define BUFFER_LEN 1024

// Redirects pipe data to javascript
void thread_pipe(webview_t w, int fd, int src, int fd_in, int fd_out, int fd_err) {
	// Reads data from pipe and sends to to javascript while pipe is open
	char buf[BUFFER_LEN + 1];
	int readLen;
	while ((readLen = read(fd, buf, BUFFER_LEN)) > 0) {
		buf[readLen] = '\0';
		std::string jsCall = "Native._nativeToJs(" +
			std::to_string(fd_in) +
			",{value:[`" + buf + "`," + std::to_string(src) +
			"],done:false});";
		webview_dispatch(w, js_evalOnMain, (void*)jsCall.c_str());
	}

	// Debug prints pipe closed
	DEBUG_PRINTF("End of read for %d, closing fds: %d, %d, %d\n", fd, fd_in, fd_out, fd_err);

	// Closes connected pipes
	close(fd_in);
	close((fd == fd_out) ? fd_err : fd_out);

	// Sends close call to javascript
	std::string jsCall = "Native._nativeToJs(" + std::to_string(fd_in) + ",{done:true});";
	webview_dispatch(w, js_evalOnMain, (void*)jsCall.c_str());
}

void closeSide(int fds_in[2], int fds_out[2], int fds_err[2], int side) {
	close(fds_in[!side]);
	close(fds_out[side]);
	close(fds_err[side]);
}

void closeAll(int fds_in[2], int fds_out[2], int fds_err[2]) {
	closeSide(fds_in, fds_out, fds_err, 1);
	closeSide(fds_in, fds_out, fds_err, 0);
}

// Creates a process that runs the cmd with standard pipes going to javascript
void native_open(const char *seq, const char *req, void *arg) {
	webview_t w = (webview_t)arg;

	// Creates pipes to go between native process and javascript
	int fds_in[2];
	int fds_out[2];
	int fds_err[2];
	if (
		pipe(fds_in) == -1 ||
		pipe(fds_out) == -1 ||
		pipe(fds_err) == -1
	) {
		JS_REJECT(w, seq, "Failed to create pipes");
		closeAll(fds_in, fds_out, fds_err);
		return;
	}

	pid_t pid;
	switch ((pid = fork())) {
		// Rejects process creation if forking failed
		case -1: {
			closeAll(fds_in, fds_out, fds_err);
			JS_REJECT(w, seq, "Failed to fork process");
		}
		// Runs system call in child process with standard pipes piped to parent process
		case 0: {
			// Gets native system command from arguments
			std::string cmd = webview::detail::json_parse(req, "", 0);

			// Rejects javascript promise if command did not exist
			if (cmd.length() == 0) {
				JS_REJECT(w, seq, "No command argument");
				closeAll(fds_in, fds_out, fds_err);
				exit(EXIT_FAILURE);
			}

			// Debug prints command
			DEBUG_PRINTF("Running command: %s\n", cmd.c_str());

			// Redirects standard pipes to parent process
			closeSide(fds_in, fds_out, fds_err, 0);
			dup2(fds_in[0], STDIN_FILENO);
			dup2(fds_out[1], STDOUT_FILENO);
			dup2(fds_err[1], STDERR_FILENO);
			closeSide(fds_in, fds_out, fds_err, 1);

			// Runs native system call
			system(cmd.c_str());
			exit(EXIT_SUCCESS);
		}
		// Reads standard pipes from child process and sends it to javascript
		default: {
			// Closes child side of pipes for parent
			closeSide(fds_in, fds_out, fds_err, 1);

			// Reads childs redirected standard pipes in threads to send to javascript
			std::thread t1(&thread_pipe, w, fds_out[0], STDOUT_FILENO, fds_in[1], fds_out[0], fds_err[0]);
			std::thread t2(&thread_pipe, w, fds_err[0], STDERR_FILENO, fds_in[1], fds_out[0], fds_err[0]);
			t1.detach();
			t2.detach();

			// Resolves javascript promise with pipe file descriptors
			std::string str = "{\"fds\":[" +
				std::to_string(fds_in[1]) + "," +
				std::to_string(fds_out[0]) + "," +
				std::to_string(fds_err[0]) + "],\"pid\":" +
				std::to_string(pid) + "}";
			webview_return(w, seq, 0, str.c_str());

			break;
		}
	}
}

// Writes data to an active native command
void native_write(const char *seq, const char *req, void *arg) {
	webview_t w = (webview_t)arg;

	// Gets file descriptor from JSON arguments
	int fd = atoi(webview::detail::json_parse(req, "", 0).c_str());
	if (fd == 0) {
		JS_REJECT(w, seq, "Invalid argument: fd");
		return;
	}

	// Gets system command from JSON arguments
	std::string msg = webview::detail::json_parse(req, "", 1);
	if (msg.length() == 0) {
		JS_REJECT(w, seq, "Invalid argument: msg");
		return;
	}

	// Debug prints file descriptor and message
	DEBUG_PRINTF("Writing data to fd %d: `%s`\n", fd, msg.c_str());

	// Writes message to pipe and reject javascript promise if writing fails
	if (write(fd, msg.c_str(), msg.length()) == -1) {
		JS_REJECT(w, seq, "Failed to write data");
		return;
	}

	// Resolves javascript promise if writing to pipe succeeded
	webview_return(w, seq, 0, "");
}

// Closes the pipes from javascript to native
void native_close(const char *seq, const char *req, void *arg) {
	webview_t w = (webview_t)arg;

	// Gets file descriptors from JSON arguments
	int fd_in = atoi(webview::detail::json_parse(req, "", 0).c_str());
	int fd_out = atoi(webview::detail::json_parse(req, "", 1).c_str());
	int fd_err = atoi(webview::detail::json_parse(req, "", 2).c_str());

	// Validates file descriptor elements existing
	if (fd_in == 0 || fd_out == 0 || fd_err == 0) {
		JS_REJECT(w, seq, "Invalid arguments");
		DEBUG_PRINTF("Invalid arguments\n");
		return;
	}

	// Debug prints file descriptors
	DEBUG_PRINTF("Closing fds: %d, %d, %d\n", fd_in, fd_out, fd_err);

	// Closes file descriptors
	int fd_in_closed = close(fd_in);
	int fd_out_closed = close(fd_out);
	int fd_err_closed = close(fd_err);

	// Rejects javascript promise if any close call failed
	if (fd_in_closed == -1 || fd_out_closed == -1 || fd_err_closed == -1) {
		JS_REJECT(w, seq, "Failed to close one or more pipes");
		return;
	}

	// Resolves javascript promise if no close call failed
	webview_return(w, seq, 0, "");
}



// Reads file to string class
std::string readFile(const char* filePath) {
	std::ifstream fileStream;
	fileStream.open(filePath);
	std::stringstream stringStream;
	stringStream << fileStream.rdbuf();
	std::string str = stringStream.str();
	return str;
}

// Creates a webview from configuration file
webview_t createWebview() {
	// Sets up webview and bindings between C++ and javascript
	webview_t w = webview_create(1, NULL);
	webview_bind(w, "_jsToNative_open", native_open, w);
	webview_bind(w, "_jsToNative_write", native_write, w);
	webview_bind(w, "_jsToNative_close", native_close, w);
	webview_init(w, \
		"function Native(cmd, callback) {\n"
		"	this.closed = false;\n"
		"	this.fds = null;\n"
		"	this.callback = callback;\n"
		"	return _jsToNative_open(cmd).then(({fds,pid}) => {\n"
		"		this.fds = fds;\n"
		"		this.pid = pid;\n"
		"		Native._table[fds[0]] = this;\n"
		"		return this;\n"
		"	}).catch((err) => {\n"
		"		throw new Error(err);\n"
		"	});\n"
		"}\n"
		"Native.prototype.write = function (msg) {\n"
		"	return _jsToNative_write(this.fds[0], msg).catch((err) => {\n"
		"		throw new Error(err);\n"
		"	});\n"
		"};\n"
		"Native.prototype.close = function () {\n"
		"	if (this.closed) return;\n"
		"	return _jsToNative_close(...this.fds).catch((err) => {\n"
		"		throw new Error(err);\n"
		"	});\n"
		"};\n"
		"Native.os = \"" JS_OS "\";\n"
		"Native._table = Object.create(null);\n"
		"Native._nativeToJs = function(id, data) {\n"
		"	const native = Native._table[id];\n"
		"	if (native == null) return;\n"
		"	const callback = native.callback;\n"
		"	if (callback != null) callback(data);\n"
		"	if (data.done) {\n"
		"		Native._table[id] = null;\n"
		"		native.closed = true;\n"
		"	}\n"
		"};\n"
		"Native.prototype[Symbol.asyncIterator] = function () {\n"
		"	return {\n"
		"		next: () => {\n"
		"			return new Promise((resolve) => {\n"
		"				if (this.callback != null) throw new Error('Reader already in use');\n"
		"				this.callback = (data) => {\n"
		"					this.callback = null;\n"
		"					resolve(data);\n"
		"				};\n"
		"			});\n"
		"		}\n"
		"	}\n"
		"};\n"
	);

	// Gets configurations from file
	std::string conf = readFile("./config.json");
	if (conf.length() == 0) {
		printf("Unable to read config file\n");
		exit(EXIT_FAILURE);
	}
	std::string windowTitle = webview::detail::json_parse(conf, "title", 0);
	std::string windowWidth = webview::detail::json_parse(conf, "width", 0);
	std::string windowHeight = webview::detail::json_parse(conf, "height", 0);
	std::string htmlFileName = webview::detail::json_parse(conf, "path", 0);

	// Sets up webview window
	webview_set_title(w, windowTitle.c_str());
	webview_set_size(w, std::stoi(windowWidth), std::stoi(windowHeight), WEBVIEW_HINT_NONE);

	// Loads html
	std::string html = readFile(htmlFileName.c_str());
	if (html.length() == 0) {
		printf("Unable to read html file\n");
		exit(EXIT_FAILURE);
	}
	webview_set_html(w, html.c_str());

	return w;
}

int main(int argc, char** argv) {
	// chdir(dirname(argv[0]));
	webview_t w = createWebview();
	webview_run(w);
	webview_destroy(w);
}
