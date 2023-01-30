# xbatticon

A small battery status/charging indicator for X11 which is intended
to be iconified all the time to remain on the desktop.
Its icon and window title indicate the current battery status and
whether the battery is charging.

![charged](https://user-images.githubusercontent.com/9888/215555258-53074b6a-deb7-4fa0-a88f-d89e6361a5b1.png)
![discharging](https://user-images.githubusercontent.com/9888/215555294-b93ca623-3d73-446d-8400-c1f339f1a144.png)

It was written to work with
[progman](https://github.com/jcs/progman)
but should work with any X11 window manager that handles `IconicState`
hints and shows icons in a useful manner.

## License

ISC

## Dependencies

`libX11` and `libXpm`

## Compiling

Fetch the source, `make` and then `make install`
