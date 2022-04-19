# sxcs - Simple X Color Sniper

Color picker for X11, also has zoom feature. Outputs TAB separated `hex`,
`rgb`, and `hsl` colors to `stdout` upon selection.

![preview.gif](preview.gif)

## Usage

<kbd>Button1</kbd> will select and print the color,
any other mouse button will quit sxcs.

Output format can be chosen via cli argument.
Zoom/magnification can be disabled via `--mag-none`.

```console
$ sxcs --rgb --mag-none
color:  rgb: 22 158 111
```

To see a list of all available cli arguments:

```console
$ sxcs --help
```

## Building

- Build Dependencies:
  * C89 compiler
  * make
  * necessary headers

- Runtime Dependencies:
  * C standard library
  * Xlib
  * Xrender
  * XComposite

```console
$ make
# sudo make install
```

## TODOs

* Make it viable as a standalone magnifying application as well.

grep the source to find more TODOs:

```console
$ grep -Hn -E 'TODO|FIXME' sxcs.c
```
