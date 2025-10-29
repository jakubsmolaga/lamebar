# lamebar
hyper-minimalistic status bar for Wayland compositors  

## overview
lamebar is intended to be used as an overlay toggled with the "super" key (or any other key of your choice)  
the main goals are:
- distributed as a single binary
- no external dependencies
- lightweight with low system load
- only basic functionality (date, time, battery level etc.)
- low configurability (should "just work" out of the box)

## screenshots
<img width="960" height="540" alt="image" src="https://github.com/user-attachments/assets/32340cdb-c1fe-42d7-900c-d760f1a70601" />

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
$ mkdir -p build
$ make generate
$ make release
```

## usage
```bash
$ lamebar --help # show help
$ lamebar # start the daemon
$ lamebar show # send a 'show' message to the running daemon
$ lamebar hide # send a 'hide' message to the running daemon
```
