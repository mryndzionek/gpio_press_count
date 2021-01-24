#!/bin/sh

pidfile="/var/run/powerbutton.pid"

gpio_press_count 4 3 5 &
echo $! > $pidfile

echo "PID is: $(cat $pidfile)"
wait $(cat $pidfile)

code=$?
echo "wait exited with code: $code"
rm $pidfile
sync

if [ "$code" -eq "3" ]
then
  reboot
elif [ "$code" -eq "5" ]
then
  poweroff
else
  echo "Unknown exit code"
fi

