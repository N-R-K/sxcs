# sxcs - Simple X Color Sniper

[![CodeBerg](https://img.shields.io/badge/Hosted_at-Codeberg-%232185D0?style=flat-square&logo=CodeBerg)](https://codeberg.org/NRK/sxcs)

Color picker and magnifier for X11.

![preview](https://images2.imgbox.com/4c/d0/LC6pYmrB_o.gif)

## Usage

<kbd>Button1</kbd> will select and print the color to `stdout`, the output is
TAB separated `hex`, `rgb`, and `hsl`.
<kbd>Scroll Up/Down</kbd> will zoom in and out.
Any other mouse button will quit sxcs.

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

Color output can be disabled via `--color-none`, which more or less turns
`sxcs` into a magnifier.

The magnifying window can be customized via using `--mag-filters <filter-list>`,
where `filter-list` is a comma separated list of filters to apply. The
filter-list will be applied in order, as given by the user.

The default filter list is the following:

```console
$ sxcs --mag-filters "grid,circle,xhair"
```

Following are a couple more examples:

```console
$ sxcs --mag-filters "square,xhair"
```

<img width="256" height="256" src="https://images2.imgbox.com/a4/ff/yUOGtRnn_o.png"/>

```console
$ sxcs --mag-filters "grid,xhair"
```

<img width="256" height="256" src="https://images2.imgbox.com/73/f6/ScP4MQT2_o.png"/>

Consult the manpage to see a list of all available cli arguments and filters:

```console
$ man sxcs
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

NOTE: the Makefile assumes `CC` is set to either `gcc` or `clang`. Use the
following if you're using another compiler and experience build failure:

```console
$ make CC=${your_c_compiler} CFLAGS=""
```

## Limitation

Cursor size bigger than 255x255 causes visual glitches, it seems to be a
X11/Xcursor limitation.

One alternative would be using XComposite and using an `override_redirect`
window. Which is what was being done (incorrectly) before commit
[33490dd](https://codeberg.org/NRK/sxcs/commit/33490ddf9164655bf6decafa6f85082e413fa333).
I suspect doing this correctly would require way too much code, probably above
my self imposed limit of ~800 SLoC for this project.

## TODOs

grep the source to find TODOs:

```console
$ grep -Hn -E 'TODO|FIXME' sxcs.c
```
