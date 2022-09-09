# Webview CLI

A super easy way to create lightweight Graphical User Interfaces (GUIs) for command line tools.
Interfaces are built in HTML with access to the command line through javascript.

It does not rely on heavy dependencies like Electron, NW or NodeJS.



## Build

### Example

#### MacOS bundle

1. Run these commands in the terminal.
```sh
git clone https://github.com/FanatiQS/webview_cli.git
cd webview_cli
make
cp example/example.html webview_cli.app/example.html
cp example/example.sh webview_cli.app/example.sh
cp example/config.json webview_cli.app/config.json
```
2. Run `webview_cli.app` from finder.

#### MacOS binary

1. Run these commands in the terminal.
```sh
git clone https://github.com/FanatiQS/webview_cli.git
cd webview_cli
make webview_cli
```
2. There are 2 alternatives for execution with binary.
	* Run `example/webview` from finder.
	* Run `cd example` in the terminal to switch to examples directory and then `../webview_cli` to execute the webview_cli in the current directory.

Note that the bundles executable can be used in the same way

#### Linux

Currently untested but might be supported.

#### Windows

Currently unsupported.



## Javascript API
The javascript API is very basic but powerful with only a single class.

### Running a command
Getting data from a native command.

```js
for await ([msg] of await new Native("ls")) {
	console.log(msg);
}
```

### Using both STDOUT and STDERR
Getting data from both STDOUT and STDERR.

```js
for await ([msg, src] of await new Native("ls; ls invalid_directory")) {
	if (src == 2) {
		console.error(msg);
	}
	else {
		console.log(msg);
	}
}
```

### Write to a process
Writing data to an active process.

```js
const native = await new Native("echo Enter your name; read; echo Hi ${REPLY}");
console.log((await native.read()).value[0]);
native.write("John Doe\n");
for await ([msg] of native) {
	console.log(msg);
}
console.log("===Closed===");
```

### Using callbacks
Reading using callbacks.

```js
await new Native("ls", function (data) {
	if (data.done) return;
	const msg = data.value[0];
	console.log(msg);
});
```

### Process ID
Getting the processes ID (pid).

```js
const native = await new Native("ping 127.0.0.1");
console.log(native.pid);
```

### Close process
Close an active native process.

```js
const native = await new Native("ping 127.0.0.1");
native.close();
```



## Current working directory

If the executable is launched without an attached TTY (command line), the current working directory will be the same as the parent directory of the executable.
When launched through a TTY, the current working directory from the command line will remain.



## Config file

Configuration of the webview window can be done with an optional config file.
It is a JSON file located in the current working directory and is called `config.json`.

The properties of the config file are so far undocumented.
