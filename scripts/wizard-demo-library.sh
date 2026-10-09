#!/usr/bin/env bash
# wizard-demo-library.sh DIR — a small made-up music library for screenshots (wizard-previews.sh):
# a few albums of synthesized tracks (ffmpeg) with tags, generated covers (ImageMagick; embedded
# and as folder.jpg) and synced lyrics (.lrc sidecars). Nothing in it is anyone's real music or art.
set -euo pipefail
DIR="$1"; mkdir -p "$DIR"
FONT=(); [[ "$(magick -list font 2>/dev/null)" == *"Font: Adwaita-Sans-Bold"$'\n'* ]] && FONT=(-font Adwaita-Sans-Bold)

# artist | album | year | genre | cover colours (top, bottom) | base note Hz | track titles (;)
ALBUMS=(
  "The Glass Pilots|Neon Harbor|2021|Synthpop|#ff5f6d|#3a1c71|220|Harbor Lights;Static Hearts;Neon Tide;Last Ferry Home"
  "Mira Vale|Low Orbit|2019|Dream Pop|#43cea2|#185a9d|196|Low Orbit;Gravity Well;Satellite Song;Weightless"
  "Northbound Static|Paper Satellites|2023|Indie Rock|#f7971e|#5b2a86|247|Paper Satellites;Copper Wire;Signal Fires;Long Way North"
  "Ada Lumen|Midnight Cartography|2018|Electronica|#00c6ff|#0b1d3a|175|Night Maps;Blue Hour;Coordinates;Dawn Survey"
)
LYRICS=("Out where the city meets the water" "Every light is a story told" "We keep on running through the static"
  "Hold the signal, don't let go" "Turn it up and let it carry" "All the way back home")

# A different motif on each cover: an outer and an inner shape.
SHAPES=("circle 300,250 300,90|circle 300,250 300,30"
  "circle 300,250 300,140|rectangle 0,300 600,330"
  "polygon 300,60 480,380 120,380|polygon 300,140 420,350 180,350"
  "rectangle 150,80 450,380|rectangle 210,140 390,320")
for ai in "${!ALBUMS[@]}"; do
  IFS='|' read -r artist album year genre c1 c2 hz titles <<<"${ALBUMS[$ai]}"
  IFS='|' read -r outer inner <<<"${SHAPES[$ai]}"
  ad="$DIR/$artist/$album"; mkdir -p "$ad"
  magick -size 600x600 "gradient:$c1-$c2" \
    \( -size 600x600 xc:none -fill '#ffffff22' -draw "$outer" -fill '#ffffff1c' -draw "$inner" \) -composite \
    "${FONT[@]}" -fill white -gravity south -pointsize 46 -annotate +0+92 "$album" \
    -fill '#ffffffb0' -pointsize 28 -annotate +0+48 "$artist" -quality 90 "$ad/folder.jpg"
  IFS=';' read -ra ts <<<"$titles"
  for i in "${!ts[@]}"; do
    n=$((i + 1)); t="${ts[$i]}"; f="$ad/$(printf %02d "$n") - $t.mp3"; [ -s "$f" ] && continue
    f0=$(( hz + i * 22 )); dur=$(( 150 + (i * 37 + ${#t} * 11) % 120 ))
    # A chord with a slow tremolo over a soft pulse and a noise hi-hat: something across the whole
    # spectrum for the visualisations.
    ffmpeg -nostdin -loglevel error -f lavfi -t "$dur" \
      -i "aevalsrc='0.22*sin(2*PI*$f0*t)*(0.6+0.4*sin(2*PI*0.5*t))+0.16*sin(2*PI*$f0*1.26*t)+0.12*sin(2*PI*$f0*1.5*t)+0.18*sin(2*PI*$f0/2*t)*exp(-6*mod(t,0.5))+0.05*sin(2*PI*$f0*4*t)*exp(-12*mod(t+0.25,0.5))+0.12*(random(0)-0.5)*exp(-28*mod(t,0.25))':s=44100" \
      -i "$ad/folder.jpg" -map 0 -map 1 -c:a libmp3lame -b:a 192k -c:v mjpeg -disposition:v attached_pic \
      -id3v2_version 3 -metadata title="$t" -metadata artist="$artist" -metadata album_artist="$artist" \
      -metadata album="$album" -metadata date="$year" -metadata genre="$genre" -metadata track="$n/${#ts[@]}" \
      -metadata:s:v comment="Cover (front)" "$f"
    { for k in "${!LYRICS[@]}"; do printf '[%02d:%02d.00]%s\n' $(( (4 + k * 6) / 60 )) $(( (4 + k * 6) % 60 )) "${LYRICS[$k]}"; done; } > "${f%.mp3}.lrc"
  done
done
