#include <stdlib.h> // STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, system, NULL, malloc, exit, EXIT_FAILURE, EXIT_SUCCESS, atoi, free
#include <stdio.h> // printf, FILE, fopen, fseek, fclose, ftell, rewind, fread, fflush, stdout
#include <string.h> // strlen, strerror
#include <errno.h> // errno

#include <unistd.h> // write, close, read, pipe, fork, dup2, chdir, pid_t, isatty
#include <libgen.h> // dirname, basename

#include <thread> // std::thread

#include "./webview.h"



int json_parse_int(const char* str, const char* key, int index) {
	return atoi(webview::detail::json_parse(str, key, index).c_str());
}



#define JS_OS "macos_x86_64"



// Only enable debug prints if DEBUG macro is defined
#ifdef DEBUG
#define DEBUG_PRINTF(...) do {printf(__VA_ARGS__);} while (0)
#else
#define DEBUG_PRINTF(...) do {} while (0)
#endif



// Evaluates javascript code on main thread to not cause crashes
void js_evalOnMain(webview_t w, void* arg) {
	std::string* str = (std::string*)arg;
	webview_eval(w, str->c_str());
	delete str;
}

// Rejects javascript promise with error message
#define JS_REJECT(w, seq, msg) webview_return(w, seq, 1, "new Error(\"" msg "\")")

// Size of pipe read buffer
#define BUFFER_LEN 1024

// Redirects pipe data to javascript
void thread_pipe(webview_t w, int fd, int src, int fd_in, int fd_out, int fd_err) {
	// Reads data from pipe and sends to to javascript while pipe is open
	char buf[BUFFER_LEN + 1];
	int readLen;
	while ((readLen = read(fd, buf, BUFFER_LEN)) > 0) {
		buf[readLen] = '\0';
		DEBUG_PRINTF("Read data from fd %d: %s\n", fd, buf);
		std::string* jsCall = new std::string("Native._nativeToJs(" +
			std::to_string(fd_in) +
			",{value:[`" + buf + "`," + std::to_string(src) +
			"],done:false});");
		webview_dispatch(w, js_evalOnMain, (void*)jsCall);
	}

	// Debug prints pipe closed
	DEBUG_PRINTF("End of read for %d, closing fds: %d, %d, %d\n", fd, fd_in, fd_out, fd_err);

	// Closes connected pipes
	close(fd_in);
	close((fd == fd_out) ? fd_err : fd_out);

	// Sends close call to javascript
	std::string* jsCall = new std::string("Native._nativeToJs(" + std::to_string(fd_in) + ",{done:true});");
	webview_dispatch(w, js_evalOnMain, (void*)jsCall);
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

#define PIPE_REJECT(w, seq)\
	do {\
		JS_REJECT(w, seq, "Failed to create pipes");\
		DEBUG_PRINTF("Failed to create pipes\n");\
	} while (0)

// Creates a process that runs the cmd with standard pipes going to javascript
void native_open(const char *seq, const char *req, void *arg) {
	webview_t w = (webview_t)arg;

	// Creates pipes to go between system process and javascript
	int fds_in[2];
	int fds_out[2];
	int fds_err[2];
	if (pipe(fds_in) == -1) {
		PIPE_REJECT(w, seq);
		return;
	}
	if (pipe(fds_out) == -1) {
		PIPE_REJECT(w, seq);
		close(fds_in[0]);
		close(fds_in[1]);
		return;
	}
	if (pipe(fds_err) == -1) {
		PIPE_REJECT(w, seq);
		close(fds_in[0]);
		close(fds_in[1]);
		close(fds_out[0]);
		close(fds_out[1]);
		return;
	}

	// Gets system command from arguments
	std::string cmd = webview::detail::json_parse(req, "", 0);

	// Rejects javascript promise if command did not exist
	if (cmd.length() == 0) {
		JS_REJECT(w, seq, "No command argument");
		closeAll(fds_in, fds_out, fds_err);
		return;
	}

	pid_t pid;
	switch ((pid = fork())) {
		// Rejects process creation if forking failed
		case -1: {
			closeAll(fds_in, fds_out, fds_err);
			JS_REJECT(w, seq, "Failed to fork process");
			DEBUG_PRINTF("Failed to fork process\n");
			return;
		}
		// Runs system call in child process with standard pipes piped to parent process
		case 0: {
			// Debug prints command
			DEBUG_PRINTF("Running command: %s\n", cmd.c_str());
#ifdef DEBUG
			fflush(stdout);
#endif

			// Redirects standard pipes to parent process
			closeSide(fds_in, fds_out, fds_err, 0);
			dup2(fds_in[0], STDIN_FILENO);
			dup2(fds_out[1], STDOUT_FILENO);
			dup2(fds_err[1], STDERR_FILENO);
			closeSide(fds_in, fds_out, fds_err, 1);

			// Runs system call
			system(cmd.c_str());
			exit(EXIT_SUCCESS);
		}
	}

	// Closes child side of pipes for parent
	closeSide(fds_in, fds_out, fds_err, 1);

	// Debug prints pipes file descriptors and forked process id
	DEBUG_PRINTF("Created process %d with fds: %d %d %d\n", pid, fds_in[1], fds_out[0], fds_err[0]);

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
}

// Writes data to an active system command
void native_write(const char *seq, const char *req, void *arg) {
	webview_t w = (webview_t)arg;

	// Gets file descriptor from JSON arguments
	int fd = json_parse_int(req, "", 0);
	if (fd == 0) {
		JS_REJECT(w, seq, "Invalid argument: fd");
		DEBUG_PRINTF("Rejected write for invalid fd argument: %s\n", req);
		return;
	}

	// Gets system command from JSON arguments
	std::string msg = webview::detail::json_parse(req, "", 1);
	if (msg.length() == 0) {
		JS_REJECT(w, seq, "Invalid argument: msg");
		DEBUG_PRINTF("Rejected write for invalid msg argument: %s\n", req);
		return;
	}

	// Debug prints file descriptor and message
	DEBUG_PRINTF("Writing data to fd %d: `%s`\n", fd, msg.c_str());

	// Writes message to pipe and reject javascript promise if writing fails
	if (write(fd, msg.c_str(), msg.length()) == -1) {
		JS_REJECT(w, seq, "Failed to write data");
		DEBUG_PRINTF("Failed to write to pipe: %d\n", fd);
		return;
	}

	// Resolves javascript promise if writing to pipe succeeded
	webview_return(w, seq, 0, "");
}

// Closes the pipes from javascript to system command
void native_close(const char *seq, const char *req, void *arg) {
	webview_t w = (webview_t)arg;

	// Gets file descriptors from JSON arguments
	int fd_in = json_parse_int(req, "", 0);
	int fd_out = json_parse_int(req, "", 1);
	int fd_err = json_parse_int(req, "", 2);

	// Validates file descriptor elements existing
	if (fd_in == 0 || fd_out == 0 || fd_err == 0) {
		JS_REJECT(w, seq, "Invalid arguments");
		DEBUG_PRINTF("Rejected close for invalid fd argument: %s\n", req);
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
		DEBUG_PRINTF(
			"Failed to close one or more pipes: %d, %d, %d %s\n",
			fd_in_closed, fd_out_closed, fd_err_closed, strerror(errno)
		);
		return;
	}

	// Resolves javascript promise if no close call failed
	webview_return(w, seq, 0, "");
}



// Reads file to allocated buffer
char* readFile(const char* filePath) {
	// Opens file
	FILE* file = fopen(filePath, "r");
	if (file == NULL) return NULL;

	// Gets length of files content
	if (fseek(file, 0, SEEK_END)) {
		fclose(file);
		return NULL;
	}
	size_t len = ftell(file);
	if (len == -1) {
		fclose(file);
		return NULL;
	}
	rewind(file);

	// Allocates buffer
	char* buf = (char*)malloc(len + 1);
	if (buf == NULL) {
		fclose(file);
		return NULL;
	}

	// Reads files content to buffer
	size_t readLen = fread(buf, 1, len, file);
	if (readLen != len) {
		fclose(file);
		free(buf);
		return NULL;
	}
	buf[readLen] = '\0';

	// Closes the file before returning the buffer
	fclose(file);
	return buf;
}

// Creates a webview from configuration file
webview_t createWebview(const char* fileName) {
	// Sets up webview and bindings between C++ and javascript
	webview_t w = webview_create(1, NULL);
	webview_bind(w, "_jsToNative_open", native_open, w);
	webview_bind(w, "_jsToNative_write", native_write, w);
	webview_bind(w, "_jsToNative_close", native_close, w);
	webview_init(w, \
		"'use strict';\n"
		"function Native(cmd, callback) {\n"
		"	if (this == null) return new Native(...arguments);\n"
		"	this.pid = null;\n"
		"	this.closed = false;\n"
		"	this.fds = null;\n"
		"	this.callback = callback;\n"
		"	return _jsToNative_open(cmd).then(({fds,pid}) => {\n"
		"		this.fds = fds;\n"
		"		this.pid = pid;\n"
		"		Native._table[fds[0]] = this;\n"
		"		return this;\n"
		"	});\n"
		"}\n"
		"Native.prototype.write = function (msg) {\n"
		"	if (this.closed) throw new Error('Command has closed its pipe');\n"
		"	if (!this.fds) throw new Error('Command has not finished opening')\n"
		"	return _jsToNative_write(this.fds[0], msg);\n"
		"};\n"
		"Native.prototype.close = function () {\n"
		"	if (this.closed) throw new Error('Command has closed its pipe');\n"
		"	if (!this.fds) throw new Error('Command has not finished opening');\n"
		"	return _jsToNative_close(...this.fds);\n"
		"};\n"
		"Native.prototype.next = function () {\n"
		"	return new Promise((resolve) => {\n"
		"		if (this.callback != null) throw new Error('Reader already in use');\n"
		"		this.callback = (data) => {\n"
		"			this.callback = null;\n"
		"			resolve(data);\n"
		"		};\n"
		"	});\n"
		"};\n"
		"Native.prototype.read = function () {\n"
		"	return this.next();\n"
		"};\n"
		"Native.prototype[Symbol.asyncIterator] = function () {\n"
		"	return this;\n"
		"};\n"
		"Native.os = \"" JS_OS "\";\n"
		"Native._table = Object.create(null);\n"
		"Native._nativeToJs = function(id, data) {\n"
		"	const native = Native._table[id];\n"
		"	if (native == null) return;\n"
		"	const callback = native.callback;\n"
		"	if (callback != null) callback(data);\n"
		"	if (data.done) {\n"
		"		delete Native._table[id];\n"
		"		native.closed = true;\n"
		"	}\n"
		"};\n"
	);

	// Gets configurations from file
	char* conf = readFile("./config.json");

	// Sets webview window title based on config or executable file name
	std::string windowTitle;
	if (
		conf != NULL &&
		((windowTitle = webview::detail::json_parse(conf, "title", 0)).length() != 0)
	) {
		webview_set_title(w, windowTitle.c_str());
	}
	else {
		webview_set_title(w, fileName);
	}

	// Sets initial webview window size
	int windowWidth;
	int windowHeight;
	if (
		conf != NULL &&
		((windowWidth = json_parse_int(conf, "width", 0)) != 0) &&
		((windowHeight = json_parse_int(conf, "height", 0)) != 0)
	) {
		webview_set_size(w, windowWidth, windowHeight, WEBVIEW_HINT_NONE);
	}
	else {
		webview_set_size(w, 480, 320, WEBVIEW_HINT_NONE);
	}

	// Loads html from file
	char* html;
	if (
		(conf != NULL && ((html = readFile(webview::detail::json_parse(conf, "path", 0).c_str())) != NULL)) ||
		((html = readFile((std::string(fileName) + ".html").c_str())) != NULL) ||
		((html = readFile("index.html")) != NULL)
	) {
		webview_set_html(w, html);
		free(html);
	}
	// Loads failed to load message
	else {
		webview_set_html(w, "Failed to read file");
	}

	// Releases config file content memory
	if (conf != NULL) {
		free(conf);
	}

	return w;
}

int main(int argc, char** argv) {
	// Change CWD to directory of executable if it is not running on a command line
	if (
		!isatty(STDIN_FILENO) &&
		!isatty(STDOUT_FILENO) &&
		!isatty(STDERR_FILENO)
	) {
		chdir(dirname(argv[0]));
	}

	// Set up and run webview
	webview_t w = createWebview(basename(argv[0]));
	webview_run(w);
	webview_destroy(w);
}
