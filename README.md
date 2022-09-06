# Webview CLI

This tool makes it super easy to create Graphical User Interfaces (GUIs) for command line tools.

## Usage

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
