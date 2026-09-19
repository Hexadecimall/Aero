export PS1='aero:\w # '
case "$(tty)" in
    /dev/hvc*)
        export TERM=xterm-256color
        consoleRows=24
        consoleColumns=80
        for argument in $(cat /proc/cmdline); do
            case "$argument" in
                aero.rows=*) consoleRows=${argument#*=} ;;
                aero.columns=*) consoleColumns=${argument#*=} ;;
            esac
        done
        stty rows "$consoleRows" cols "$consoleColumns"
        unset consoleRows consoleColumns argument
        ;;
    *) export TERM=linux ;;
esac
printf '\nAero Linux\nUse poweroff to shut down. Network status: cat /run/network.log\n\n'
