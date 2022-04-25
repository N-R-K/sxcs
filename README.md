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
rgb:	22 158 111
```

Copying the hex output to clipboard (using `xclip`):

```console
$ sxcs -o --hex | cut -f 2 | xclip -in -selection clipboard
```

The magnifying window can be customized via using `--mag-filters
<filter-list>`, where `filter-list` is a comma separated list of filters to
apply in order.

The default filter list is the following:

```console
$ sxcs --mag-filters "grid,circle,crosshair_square"
```

Following are a couple more examples:

```console
$ sxcs --mag-filters "square_border,crosshair_square"
```

```console
$ sxcs --mag-filters "grid,crosshair_square"
```

To see a list of all available cli arguments and filters:

```console
$ sxcs --help
```

## Building

- Build Dependencies:
  * C89 compiler
  * make
  * necessary headers

- Runtime Dependencies:
  * POSIX 2001 C standard library
  * Xlib
  * Xcursor

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
