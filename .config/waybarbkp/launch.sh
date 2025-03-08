#!/bin/sh

killall waybar
pkill waybar 
sleep 2

if [[$USER = "bigwhoman"]]
then 
	waybar -c ~/dotfiles/waybar/config & -s ~/dotfiles/waybar/style.css
else
	waybar &
fi
