# Check the current force_no_cursor state
current_state=$(hyprctl getoption input:force_no_cursor | grep "int: " | awk '{print $2}')

# Toggle state (0 is false, 1 is true)
if [ "$current_state" -eq 0 ]; then
    hyprctl keyword input:force_no_cursor true
else
    hyprctl keyword input:force_no_cursor false
fi
