# lamebar
hyper-minimalistic status bar for Wayland compositors

## quick start (hyprland)
install from the AUR
```
$ yay -S lamebar
```
add the following to your hyprland config
```
# ~/.config/hypr/hyprland.conf
exec-once = lamebar
bindt = , SUPER_L, exec, lamebar show
bindirt = , SUPER_L, exec, lamebar hide
```

## building from source
```
$ git clone https://github.com/jakubsmolaga/lamebar.git
$ cd lamebar
$ make generate
$ make release
```

## usage
```bash
$ lamebar # start the daemon
$ lamebar show # send a 'show' message to the running daemon
$ lamebar hide # send a 'hide' message to the running daemon
```
