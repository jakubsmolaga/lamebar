# lamebar
hyper-minimalistic status bar for Wayland compositors

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
$ make generate
$ make release
```

## usage
```bash
$ lamebar # start the daemon
$ lamebar show # send a 'show' message to the running daemon
$ lamebar hide # send a 'hide' message to the running daemon
```
