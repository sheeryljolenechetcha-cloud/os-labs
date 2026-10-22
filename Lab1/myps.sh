for d in /proc/[0-9]*; do
  state=$(awk '/^State:/ {print $2}' "$d/status" 2>/dev/null)
  echo "$(basename "$d") $(cat "$d/comm" 2>/dev/null) $state"
done | head -20
